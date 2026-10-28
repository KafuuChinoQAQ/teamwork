/* ELF image mapping and function-symbol collection. */

#include "cfgtool.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int range_valid(const ElfImage *image, uint64_t offset, uint64_t length)
{
    return offset <= image->size && length <= image->size - (size_t)offset;
}

static char *copy_string(const char *source, size_t length)
{
    char *result = (char *)malloc(length + 1);
    if (result == NULL) {
        return NULL;
    }
    memcpy(result, source, length);
    result[length] = '\0';
    return result;
}

static int valid_string_offset(const char *table, size_t table_size, size_t offset)
{
    if (offset >= table_size) {
        return 0;
    }
    return memchr(table + offset, '\0', table_size - offset) != NULL;
}

void unmap_image(ElfImage *image)
{
    if (image->bytes != NULL && image->size != 0) {
        munmap((void *)image->bytes, image->size);
    }
    if (image->fd >= 0) {
        close(image->fd);
    }
    memset(image, 0, sizeof(*image));
    image->fd = -1;
}

int map_image(const char *path, ElfImage *image)
{
    struct stat st;
    memset(image, 0, sizeof(*image));
    image->fd = -1;

    image->fd = open(path, O_RDONLY);
    if (image->fd < 0 || fstat(image->fd, &st) != 0 || st.st_size <= 0) {
        fprintf(stderr, "cannot open ELF file '%s': %s\n", path,
                strerror(errno));
        unmap_image(image);
        return -1;
    }
    image->size = (size_t)st.st_size;
    image->bytes = (const unsigned char *)mmap(NULL, image->size, PROT_READ,
                                                MAP_PRIVATE, image->fd, 0);
    if (image->bytes == MAP_FAILED) {
        image->bytes = NULL;
        fprintf(stderr, "cannot map '%s': %s\n", path, strerror(errno));
        unmap_image(image);
        return -1;
    }
    if (image->size < sizeof(Elf64_Ehdr) ||
        memcmp(image->bytes, ELFMAG, SELFMAG) != 0 ||
        image->bytes[EI_CLASS] != ELFCLASS64 ||
        image->bytes[EI_DATA] != ELFDATA2LSB) {
        fprintf(stderr, "'%s' is not a little-endian ELF64 file\n", path);
        unmap_image(image);
        return -1;
    }
    const Elf64_Ehdr *header = (const Elf64_Ehdr *)image->bytes;
    if (header->e_machine != EM_X86_64 || header->e_shentsize != sizeof(Elf64_Shdr) ||
        header->e_shnum == 0 ||
        !range_valid(image, header->e_shoff,
                     (uint64_t)header->e_shentsize * header->e_shnum) ||
        header->e_shstrndx >= header->e_shnum) {
        fprintf(stderr, "ELF must contain a valid x86-64 section table\n");
        unmap_image(image);
        return -1;
    }
    const Elf64_Shdr *sections = (const Elf64_Shdr *)(image->bytes + header->e_shoff);
    const Elf64_Shdr *names_section = &sections[header->e_shstrndx];
    if (!range_valid(image, names_section->sh_offset, names_section->sh_size)) {
        fprintf(stderr, "invalid ELF section-name table\n");
        unmap_image(image);
        return -1;
    }
    const char *section_names = (const char *)(image->bytes + names_section->sh_offset);
    size_t section_names_size = names_section->sh_size;
    for (unsigned int i = 0; i < header->e_shnum; ++i) {
        const Elf64_Shdr *section = &sections[i];
        if (!range_valid(image, section->sh_offset, section->sh_size) ||
            section->sh_name >= section_names_size ||
            !valid_string_offset(section_names, section_names_size, section->sh_name)) {
            continue;
        }
        if (strcmp(section_names + section->sh_name, ".text") == 0 &&
            (section->sh_flags & SHF_EXECINSTR) != 0 && section->sh_size != 0) {
            image->text_file_offset = section->sh_offset;
            image->text_address = section->sh_addr;
            image->text_size = section->sh_size;
            break;
        }
    }
    if (image->text_size == 0) {
        fprintf(stderr, "ELF has no executable .text section\n");
        unmap_image(image);
        return -1;
    }
    return 0;
}

static int compare_functions(const void *left, const void *right)
{
    const Function *a = (const Function *)left;
    const Function *b = (const Function *)right;
    if (a->address < b->address) return -1;
    if (a->address > b->address) return 1;
    if (a->size < b->size) return -1;
    if (a->size > b->size) return 1;
    return strcmp(a->name, b->name);
}

void free_functions(Function *functions, int count)
{
    if (functions == NULL) return;
    for (int i = 0; i < count; ++i) free(functions[i].name);
    free(functions);
}

static int append_function(Function **functions, int *count, int *capacity,
                           const char *name, size_t name_length,
                           uint64_t address, uint64_t size)
{
    if (*count == *capacity) {
        int new_capacity = (*capacity == 0) ? 32 : (*capacity * 2);
        Function *grown = (Function *)realloc(*functions,
                                               (size_t)new_capacity * sizeof(Function));
        if (grown == NULL) return -1;
        *functions = grown;
        *capacity = new_capacity;
    }
    char *function_name = copy_string(name, name_length);
    if (function_name == NULL) return -1;
    (*functions)[*count].name = function_name;
    (*functions)[*count].address = address;
    (*functions)[*count].size = size;
    (*functions)[*count].file_offset = 0;
    ++*count;
    return 0;
}

int collect_functions(const ElfImage *image, Function **result, int *result_count)
{
    const Elf64_Ehdr *header = (const Elf64_Ehdr *)image->bytes;
    const Elf64_Shdr *sections = (const Elf64_Shdr *)(image->bytes + header->e_shoff);
    Function *functions = NULL;
    int count = 0;
    int capacity = 0;

    for (unsigned int i = 0; i < header->e_shnum; ++i) {
        const Elf64_Shdr *section = &sections[i];
        if ((section->sh_type != SHT_SYMTAB && section->sh_type != SHT_DYNSYM) ||
            section->sh_entsize < sizeof(Elf64_Sym) ||
            section->sh_link >= header->e_shnum ||
            !range_valid(image, section->sh_offset, section->sh_size)) {
            continue;
        }
        const Elf64_Shdr *strings_section = &sections[section->sh_link];
        if (!range_valid(image, strings_section->sh_offset, strings_section->sh_size)) {
            continue;
        }
        const char *strings = (const char *)(image->bytes + strings_section->sh_offset);
        size_t string_size = strings_section->sh_size;
        size_t symbol_count = section->sh_size / section->sh_entsize;
        for (size_t j = 0; j < symbol_count; ++j) {
            const Elf64_Sym *symbol = (const Elf64_Sym *)(image->bytes +
                                      section->sh_offset + j * section->sh_entsize);
            if (ELF64_ST_TYPE(symbol->st_info) != STT_FUNC ||
                symbol->st_value < image->text_address ||
                symbol->st_value >= image->text_address + image->text_size ||
                symbol->st_name >= string_size ||
                !valid_string_offset(strings, string_size, symbol->st_name)) {
                continue;
            }
            const char *name = strings + symbol->st_name;
            size_t name_length = strlen(name);
            if (name_length == 0) name = "anonymous";
            if (name_length == 0) name_length = strlen(name);
            if (append_function(&functions, &count, &capacity, name, name_length,
                                symbol->st_value, symbol->st_size) != 0) {
                free_functions(functions, count);
                return -1;
            }
        }
    }

    if (count == 0) {
        if (append_function(&functions, &count, &capacity, "whole_text", 10,
                            image->text_address, image->text_size) != 0) {
            free_functions(functions, count);
            return -1;
        }
    }
    qsort(functions, (size_t)count, sizeof(Function), compare_functions);

    /* Remove aliases at the same address, retaining the largest range. */
    int unique_count = 0;
    for (int i = 0; i < count; ++i) {
        if (unique_count != 0 && functions[unique_count - 1].address == functions[i].address) {
            if (functions[i].size > functions[unique_count - 1].size) {
                free(functions[unique_count - 1].name);
                functions[unique_count - 1] = functions[i];
            } else {
                free(functions[i].name);
            }
            continue;
        }
        functions[unique_count++] = functions[i];
    }
    for (int i = 0; i < unique_count; ++i) {
        uint64_t end = image->text_address + image->text_size;
        if (i + 1 < unique_count && functions[i + 1].address < end) {
            end = functions[i + 1].address;
        }
        if (functions[i].size == 0 || functions[i].address + functions[i].size > end) {
            functions[i].size = end - functions[i].address;
        }
        if (functions[i].address < image->text_address ||
            functions[i].address >= image->text_address + image->text_size) {
            functions[i].size = 0;
        }
        if (functions[i].size > image->text_size -
                                  (functions[i].address - image->text_address)) {
            functions[i].size = image->text_size -
                                (functions[i].address - image->text_address);
        }
        functions[i].file_offset = image->text_file_offset +
                                   (functions[i].address - image->text_address);
    }
    int write_index = 0;
    for (int i = 0; i < unique_count; ++i) {
        if (functions[i].size != 0) functions[write_index++] = functions[i];
        else free(functions[i].name);
    }
    *result = functions;
    *result_count = write_index;
    return 0;
}
