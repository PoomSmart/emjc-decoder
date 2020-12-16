//
//  sbix_emjc_decode.c
//  sbix/emjc decoder
//
//  Created by cc4966 on 2018/08/19.
//
//  YPwn was here. 2020
//

#include "sbix_emjc_decode.h"

#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lzfse.h>

extern int fileno(FILE* stream);

EMJC_API uint16_t emjc_width(const uint8_t *__restrict src_buffer,
                             size_t src_size)
{
    const uint8_t *p = src_buffer;
    const uint16_t width = (uint16_t) p[8] + ((uint16_t) p[9] << 8);
    return width;
}

EMJC_API uint16_t emjc_height(const uint8_t *__restrict src_buffer,
                              size_t src_size)
{
    const uint8_t *p = src_buffer;
    const uint16_t height = (uint16_t) p[10] + ((uint16_t) p[11] << 8);
    return height;
}

EMJC_API size_t emjc_decode_buffer_size(const uint8_t *__restrict src_buffer,
                                        size_t src_size)
{
    const uint8_t *p = src_buffer;
    const size_t width = (size_t) p[8] + ((size_t) p[9] << 8);
    const size_t height = (size_t) p[10] + ((size_t) p[11] << 8);
    const uint16_t appendix_length = (uint16_t) p[12] + ((uint16_t) p[13] << 8);
    //return (width * height * (4 + sizeof(int32_t)) + height + appendix_length);
    return (height * width) + height + (height * width * 3) + appendix_length;
}

int32_t convert_to_difference(int32_t value, int32_t offset) {
    if (value & 1) {
        return -(value >> 1) - offset;
    } else {
        return (value >> 1) + offset;
    }
}

int32_t filter4_value(int32_t left, int32_t upper) {
    int32_t value = left + upper + 1;
    if (value < 0) {
        return -((-value) / 2);
    } else {
        return value / 2;
    }
}

EMJC_API int emjc_decode_buffer(uint8_t *__restrict dst_buffer,
                                const uint8_t *__restrict src_buffer,
                                size_t src_size,
                                size_t dst_size)
{
    const uint8_t *p = src_buffer;
    const size_t data_length = src_size;
    if ( p[0] != 'e' || p[1] != 'm' || p[2] != 'j' || p[3] != '1' ) {
        return -1;
    }
    // const uint16_t version = (uint16_t) p[4] + ((uint16_t) p[5] << 8);
    // const uint16_t unknown = (uint16_t) p[6] + ((uint16_t) p[7] << 8); // 0xa101
    const uint16_t width = (uint16_t) p[8] + ((uint16_t) p[9] << 8);
    const uint16_t height = (uint16_t) p[10] + ((uint16_t) p[11] << 8);
    const uint16_t appendix_length = (uint16_t) p[12] + ((uint16_t) p[13] << 8);
    // const uint16_t padding = (uint16_t) p[14] + ((uint16_t) p[15] << 8);
    const uint16_t filter_length = height;
    //const size_t dst_length = (size_t) height * width + filter_length + (size_t) height * width * 3 + appendix_length;
    const size_t dst_length = dst_size;
    uint8_t *dst = (uint8_t *) malloc(dst_length + 1);
    //const size_t len = lzfse_decode_buffer(dst, dst_length + 1, p + 16, data_length - 8 - 16, NULL);
    const size_t len = lzfse_decode_buffer(dst, dst_length, p + 16, data_length - 16, NULL);
    if ( len != dst_length ) {
        free(dst);
        return -1;
    }
    const int pixels = height * width; // alpha
    const int colors = pixels * 3; // rgb
    const uint8_t *alpha = dst;
    const uint8_t *filters = dst + pixels;
    const uint8_t *rgb = dst + pixels + filter_length;
    const uint8_t *appendix = dst + pixels + filter_length + colors;
    int32_t *buffer = (int32_t *) malloc(colors * sizeof(int32_t));
    memset(buffer, 0, colors * sizeof(int32_t));
    for (int i = 0, offset = 0; i < appendix_length; ++i) {
        offset += appendix[i] / 4;
        if (offset >= colors) {
            break;
        }
        buffer[offset++] = 128 * (appendix[i] % 4);
    }
    for (int y = 0, i = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x, ++i) {
            buffer[i * 3 + 0] = convert_to_difference(rgb[i * 3 + 0], buffer[i * 3 + 0]);
            buffer[i * 3 + 1] = convert_to_difference(rgb[i * 3 + 1], buffer[i * 3 + 1]);
            buffer[i * 3 + 2] = convert_to_difference(rgb[i * 3 + 2], buffer[i * 3 + 2]);
            switch (filters[y]) {
                case 0:
                    break;
                case 1:
                    if (x > 0 && y > 0) {
                        int32_t left = buffer[(i - 1) * 3 + 0];
                        int32_t upper = buffer[(i - width) * 3 + 0];
                        int32_t leftUpper = buffer[(i - width - 1) * 3 + 0];
                        if (abs(left - leftUpper) < abs(upper - leftUpper)) {
                            buffer[i * 3 + 0] += upper;
                            buffer[i * 3 + 1] += buffer[(i - width) * 3 + 1];
                            buffer[i * 3 + 2] += buffer[(i - width) * 3 + 2];
                        } else {
                            buffer[i * 3 + 0] += left;
                            buffer[i * 3 + 1] += buffer[(i - 1) * 3 + 1];
                            buffer[i * 3 + 2] += buffer[(i - 1) * 3 + 2];
                        }
                    } else if (x > 0) {
                        buffer[i * 3 + 0] += buffer[(i - 1) * 3 + 0];
                        buffer[i * 3 + 1] += buffer[(i - 1) * 3 + 1];
                        buffer[i * 3 + 2] += buffer[(i - 1) * 3 + 2];
                    } else if (y > 0) {
                        buffer[i * 3 + 0] += buffer[(i - width) * 3 + 0];
                        buffer[i * 3 + 1] += buffer[(i - width) * 3 + 1];
                        buffer[i * 3 + 2] += buffer[(i - width) * 3 + 2];
                    }
                    break;
                case 2:
                    if (x > 0) {
                        buffer[i * 3 + 0] += buffer[(i - 1) * 3 + 0];
                        buffer[i * 3 + 1] += buffer[(i - 1) * 3 + 1];
                        buffer[i * 3 + 2] += buffer[(i - 1) * 3 + 2];
                    }
                    break;
                case 3:
                    if (y > 0) {
                        buffer[i * 3 + 0] += buffer[(i - width) * 3 + 0];
                        buffer[i * 3 + 1] += buffer[(i - width) * 3 + 1];
                        buffer[i * 3 + 2] += buffer[(i - width) * 3 + 2];
                    }
                    break;
                case 4:
                    if (x > 0 && y > 0) {
                        buffer[i * 3 + 0] += filter4_value(buffer[(i - 1) * 3 + 0], buffer[(i - width) * 3 + 0]);
                        buffer[i * 3 + 1] += filter4_value(buffer[(i - 1) * 3 + 1], buffer[(i - width) * 3 + 1]);
                        buffer[i * 3 + 2] += filter4_value(buffer[(i - 1) * 3 + 2], buffer[(i - width) * 3 + 2]);
                    } else if (x > 0) {
                        buffer[i * 3 + 0] += buffer[(i - 1) * 3 + 0];
                        buffer[i * 3 + 1] += buffer[(i - 1) * 3 + 1];
                        buffer[i * 3 + 2] += buffer[(i - 1) * 3 + 2];
                    } else if (y > 0) {
                        buffer[i * 3 + 0] += buffer[(i - width) * 3 + 0];
                        buffer[i * 3 + 1] += buffer[(i - width) * 3 + 1];
                        buffer[i * 3 + 2] += buffer[(i - width) * 3 + 2];
                    }
                    break;
            }
            const int32_t base = buffer[i * 3 + 0];
            const int32_t p = buffer[i * 3 + 1];
            const int32_t q = buffer[i * 3 + 2];
            int32_t r, g, b;
            if (p < 0 && q < 0) {
                r = base + p / 2 - (q + 1) / 2;
                g = base + q / 2;
                b = base - (p + 1) / 2 - (q + 1) / 2;
            } else if (p < 0) {
                r = base + p / 2 - q / 2;
                g = base + (q + 1) / 2;
                b = base - (p + 1) / 2 - q / 2;
            } else if (q < 0) {
                r = base + (p + 1) / 2 - (q + 1) / 2;
                g = base + q / 2;
                b = base - p / 2 - (q + 1) / 2;
            } else {
                r = base + (p + 1) / 2 - q / 2;
                g = base + (q + 1) / 2;
                b = base - p / 2 - q / 2;
            }
            dst_buffer[i * 4 + 0] = r < 0 ? (r % 257) + 257 : (r % 257);
            dst_buffer[i * 4 + 1] = g < 0 ? (g % 257) + 257 : (g % 257);
            dst_buffer[i * 4 + 2] = b < 0 ? (b % 257) + 257 : (b % 257);
            dst_buffer[i * 4 + 3] = alpha[i];
        }
    }
    free(dst);
    free(buffer);
    return 0;
}

static size_t getFsize(FILE* f) {
	struct stat st;
	fstat(fileno(f), &st);
	return st.st_size;
}

int main(int argc, char** argv) {
	if (argc <= 1) {
		printf("Usage: %s EMJC_FILE_TO_DECODE\n", argv[0]);
		goto done;
	}

	FILE* emjcf = fopen(argv[1], "rb");
	size_t src_size = getFsize(emjcf);
	uint8_t* src_buffer = (uint8_t*)malloc(src_size);
	fread(src_buffer, 1, src_size, emjcf);
	fclose(emjcf);

	if (*(uint32_t*)(src_buffer) != 0x316A6D65) {
		puts("Error: Not an EMJC file.");
		goto err;
	}

	uint16_t width = emjc_width(src_buffer, src_size);
	uint16_t height = emjc_height(src_buffer, src_size);
	printf("Dimensions: %dx%d\n", width, height);

	size_t dst_size = emjc_decode_buffer_size(src_buffer, src_size);
	uint8_t* dst_buffer = (uint8_t*)malloc(dst_size);
	if (emjc_decode_buffer(dst_buffer, src_buffer, src_size, dst_size)) {
		puts("Error: Convert operation failed.");
		goto err;
	}

	free(src_buffer);
	char* newname = (char*)malloc(strlen(argv[1]) + 10);
	strcpy(newname, argv[1]);
	strcat(newname, ".rgba");
	FILE* rgbaf = fopen(newname, "wb");
	fwrite(dst_buffer, 1, width*height*4, rgbaf);
	fclose(rgbaf);

done: ;
	return 0;

err: ;
	return 1;
}
