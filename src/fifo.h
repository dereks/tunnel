/**
 * Copyright (c) 2014 Derek Simkowiak
 */
#ifndef FIFO_H
#define FIFO_H

// A simplistic FIFO that doesn't actually have any storage.
// Given a buffer_size, it allows you to fifo_read() or fifo_write()
// byte counts and then get the indexes for the next read or write chunk,
// including circular buffer wrapping.
//
// The caller can use these indexes to get pointers within their
// own buffer space, making it suitable for use with read(), write(),
// memcpy(), DMA ops, etc.
//
// This is not inherently threadsafe; the caller must do their own mutexing.

#include <stdlib.h>

typedef struct {
    size_t buffer_size;
    size_t read_count;
    size_t write_count;
} FIFO;

FIFO *fifo_new(size_t buffer_size);
void fifo_free(FIFO *fifo);

size_t fifo_bytes_free(FIFO *fifo);
size_t fifo_bytes_used(FIFO *fifo);

void fifo_read(FIFO *fifo, size_t byte_count);
size_t fifo_read_index(FIFO *fifo);
size_t fifo_read_size(FIFO *fifo);

void fifo_write(FIFO *fifo, size_t byte_count);
size_t fifo_write_size(FIFO *fifo);
size_t fifo_write_index(FIFO *fifo);

#endif  // FIFO_H
