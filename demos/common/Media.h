#pragma once

#include <ct/vector.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PRISMA_MEDIA_DIR
#define PRISMA_MEDIA_DIR "media"
#endif

namespace zenapp
{

inline bool mediaPath(const char* relative, char* out, size_t size)
{
    const char* root = getenv("PRISMA_MEDIA");
    if (!root || !*root) root = PRISMA_MEDIA_DIR;
    const int written = snprintf(out, size, "%s/%s", root, relative);
    return written > 0 && static_cast<size_t>(written) < size;
}

inline bool readFile(const char* path, ct::Vector<unsigned char>* out)
{
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    fseek(file, 0, SEEK_END);
    const long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length < 0)
    {
        fclose(file);
        return false;
    }
    out->resize(static_cast<size_t>(length));
    const size_t read = length > 0 ? fread(out->data(), 1, static_cast<size_t>(length), file) : 0;
    fclose(file);
    return read == static_cast<size_t>(length);
}

} // namespace zenapp
