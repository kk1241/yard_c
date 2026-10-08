#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Binary integers are decoded explicitly, independently of host byte order. */
static uint32_t little_endian(const unsigned char *p, unsigned n)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < n; ++i)
        value |= (uint32_t)p[i] << (8 * i);
    return value;
}

static unsigned char normalize(int64_t value)
{
    if (value < 0)
        return (unsigned char)((-value) % 241);
    if (value > 255)
        return (unsigned char)(value % 251);
    return (unsigned char)value;
}

int main(int argc, char **argv)
{
    const char *input_path = NULL, *output_path = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc)
            input_path = argv[++i];
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            output_path = argv[++i];
        else
            return 10;
    }
    if (!input_path || !output_path)
        return 10;

    int status = 1;
    FILE *input = fopen(input_path, "rb"), *output = NULL;
    unsigned char header[8], kernel_header[4];
    unsigned char *pixels = NULL, *result = NULL, *kernel = NULL;
    if (!input || fread(header, 1, 8, input) != 8)
        goto cleanup;
    uint32_t height = little_endian(header, 4);
    uint32_t width = little_endian(header + 4, 4);
    if ((uint64_t)height * width > SIZE_MAX / 3)
        goto cleanup;
    size_t bytes = (size_t)height * width * 3;
    pixels = malloc(bytes ? bytes : 1);
    result = malloc(bytes ? bytes : 1);
    if (!pixels || !result || fread(pixels, 1, bytes, input) != bytes ||
        fread(kernel_header, 1, 4, input) != 4)
        goto cleanup;
    uint32_t kh = little_endian(kernel_header, 2);
    uint32_t kw = little_endian(kernel_header + 2, 2);
    if (!kh || !kw || !(kh & 1) || !(kw & 1))
        goto cleanup;
    size_t kernel_size = (size_t)kh * kw;
    kernel = malloc(kernel_size);
    if (!kernel || fread(kernel, 1, kernel_size, input) != kernel_size)
        goto cleanup;

    /* The assignment's diagram applies D without reversing its axes.
       All three channels share the same kernel and zero padding.
       Even the largest uint16_t kernel fits safely in int64_t sums. */
    for (uint32_t y = 0; width != 0 && y < height; ++y) {
        int64_t top = (int64_t)y - kh / 2;
        uint32_t ky_start = top < 0 ? (uint32_t)-top : 0;
        uint32_t ky_end = top + kh > height ? (uint32_t)(height - top) : kh;
        for (uint32_t x = 0; x < width; ++x) {
            int64_t left = (int64_t)x - kw / 2;
            uint32_t kx_start = left < 0 ? (uint32_t)-left : 0;
            uint32_t kx_end = left + kw > width ? (uint32_t)(width - left) : kw;
            int64_t a = 0, b = 0, c = 0;
            for (uint32_t ky = ky_start; ky < ky_end; ++ky) {
                const unsigned char *p = pixels +
                    ((size_t)(top + ky) * width + (size_t)(left + kx_start)) * 3;
                const unsigned char *k = kernel + (size_t)ky * kw;
                for (uint32_t kx = kx_start; kx < kx_end; ++kx, p += 3) {
                    int coefficient = k[kx] < 128 ? k[kx] : (int)k[kx] - 256;
                    a += (int64_t)p[0] * coefficient;
                    b += (int64_t)p[1] * coefficient;
                    c += (int64_t)p[2] * coefficient;
                }
            }
            size_t index = ((size_t)y * width + x) * 3;
            result[index] = normalize(a);
            result[index + 1] = normalize(b);
            result[index + 2] = normalize(c);
        }
    }
    output = fopen(output_path, "wb");
    if (!output || fwrite(header, 1, 8, output) != 8 ||
        fwrite(result, 1, bytes, output) != bytes)
        goto cleanup;
    status = 0;

cleanup:
    if (input)
        fclose(input);
    if (output && fclose(output) != 0)
        status = 1;
    free(pixels);
    free(result);
    free(kernel);
    if (status)
        fprintf(stderr, "Cannot process input or write output.\n");
    return status;
}
