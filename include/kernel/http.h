#ifndef KERNEL_HTTP_H
#define KERNEL_HTTP_H

#include <stddef.h>
#include <stdint.h>

size_t http_get_url(const char *url, size_t url_length,
                    uint8_t *destination, size_t capacity);
uint64_t http_completed_requests(void);
uint64_t http_close_requests(void);

#endif
