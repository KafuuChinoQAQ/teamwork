/* CFG construction, DOT output, and the worker pool. */

#include "cfgtool.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Graph files are numbered across all workers; workers pick ids as they go. */
static int g_next_graph_id = 0;

unsigned long g_total_instructions = 0;
unsigned long g_total_blocks = 0;

static int is_prefix(unsigned char byte)
{
    return byte == 0xF0 || byte == 0xF2 || byte == 0xF3 ||
           (byte >= 0x64 && byte <= 0x67) ||
           (byte >= 0x26 && byte <= 0x3E && (byte & 7) == 6) ||
           (byte >= 0x40 && byte <= 0x4F);
}

static int opcode_needs_modrm(unsigned char opcode, int two_byte)
{
    if (two_byte) {
        return !(opcode >= 0x80 && opcode <= 0x8F) && opcode != 0x05 &&
               opcode != 0x07 && opcode != 0x0B && opcode != 0x0D &&
               opcode != 0x0F && opcode != 0x31 && opcode != 0x32 &&
               opcode != 0x33 && opcode != 0x34 && opcode != 0x35 &&
               opcode != 0x37 && opcode != 0x77 && opcode != 0xA0 &&
               opcode != 0xA1 && opcode != 0xA2 && opcode != 0xA8 &&
               opcode != 0xA9 && opcode != 0xAA && opcode != 0xAB &&
               opcode != 0xAC && opcode != 0xAD && opcode != 0xAE &&
               opcode != 0xAF;
    }
    if ((opcode >= 0x50 && opcode <= 0x5F) ||
        (opcode >= 0x70 && opcode <= 0x7F) ||
        (opcode >= 0xB0 && opcode <= 0xBF)) return 0;
    switch (opcode) {
    case 0x06: case 0x07: case 0x0E: case 0x16: case 0x17: case 0x1E:
    case 0x1F: case 0x27: case 0x2F: case 0x37: case 0x3F: case 0x60:
    case 0x61: case 0x68: case 0x6A: case 0x90: case 0x9C: case 0x9D:
    case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5:
    case 0xA6: case 0xA7: case 0xAA: case 0xAB: case 0xAC: case 0xAD:
    case 0xAE: case 0xAF: case 0xC2: case 0xC3: case 0xC8: case 0xC9:
    case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF:
    case 0xD4: case 0xD5: case 0xE0: case 0xE1: case 0xE2: case 0xE3:
    case 0xE8: case 0xE9: case 0xEA: case 0xEB: case 0xEC: case 0xED:
    case 0xEE: case 0xEF: case 0xF1: case 0xF4: case 0xF5: case 0xF8:
    case 0xF9: case 0xFA: case 0xFB: case 0xFC: case 0xFD:
        return 0;
    default:
        return 1;
    }
}

static size_t modrm_length(const unsigned char *bytes, size_t available,
                           size_t position, unsigned char modrm)
{
    if (position >= available) return 0;
    size_t length = 1;
    unsigned char mod = (unsigned char)(modrm >> 6);
    unsigned char rm = (unsigned char)(modrm & 7);
    if (mod != 3 && rm == 4) {
        if (position + length >= available) return 0;
        unsigned char sib = bytes[position + length++];
        if (mod == 0 && (sib & 7) == 5) length += 4;
    }
    if (mod == 0 && rm == 5) length += 4;
    else if (mod == 1) length += 1;
    else if (mod == 2) length += 4;
    if (position + length > available) return 0;
    return length;
}

static int decode_instruction(const unsigned char *bytes, size_t available,
                              uint64_t address, Instruction *instruction)
{
    if (available == 0) return -1;
    size_t position = 0;
    while (position < available && position < 15 && is_prefix(bytes[position])) ++position;
    if (position >= available) return -1;
    size_t prefix_length = position;
    unsigned char opcode = bytes[position++];
    int rex_w = 0;
    for (size_t p = 0; p < prefix_length; ++p) {
        if (bytes[p] >= 0x48 && bytes[p] <= 0x4F) rex_w = (bytes[p] & 8) != 0;
    }
    int two_byte = 0;
    unsigned char second_opcode = 0;
    if (opcode == 0x0F && position < available) {
        two_byte = 1;
        second_opcode = bytes[position++];
    }
    instruction->kind = INSN_NORMAL;
    instruction->target = 0;
    instruction->opcode = opcode;
    instruction->mnemonic[0] = '\0';

    if (!two_byte && opcode >= 0x70 && opcode <= 0x7F) {
        if (position + 1 > available) return -1;
        int8_t relative = (int8_t)bytes[position++];
        instruction->length = (uint8_t)position;
        instruction->target = address + position + relative;
        instruction->kind = INSN_CONDITIONAL;
        return 0;
    }
    if (!two_byte && opcode == 0xE3) {
        if (position >= available) return -1;
        int8_t relative = (int8_t)bytes[position++];
        instruction->length = (uint8_t)position;
        instruction->target = address + position + relative;
        instruction->kind = INSN_CONDITIONAL;
        return 0;
    }
    if (two_byte && second_opcode >= 0x80 && second_opcode <= 0x8F) {
        if (position + 4 > available) return -1;
        int32_t relative;
        memcpy(&relative, bytes + position, sizeof(relative));
        position += 4;
        instruction->length = (uint8_t)position;
        instruction->target = address + position + relative;
        instruction->kind = INSN_CONDITIONAL;
        return 0;
    }
    if (!two_byte && (opcode == 0xE8 || opcode == 0xE9)) {
        if (position + 4 > available) return -1;
        int32_t relative;
        memcpy(&relative, bytes + position, sizeof(relative));
        position += 4;
        instruction->length = (uint8_t)position;
        instruction->target = address + position + relative;
        instruction->kind = opcode == 0xE8 ? INSN_CALL : INSN_JUMP;
        return 0;
    }
    if (!two_byte && opcode == 0xEB) {
        if (position >= available) return -1;
        int8_t relative = (int8_t)bytes[position++];
        instruction->length = (uint8_t)position;
        instruction->target = address + instruction->length + relative;
        instruction->kind = INSN_JUMP;
        return 0;
    }
    if (!two_byte && (opcode == 0xC3 || opcode == 0xC2)) {
        if (opcode == 0xC2) {
            if (position + 2 > available) return -1;
            position += 2;
        }
        instruction->length = (uint8_t)position;
        instruction->kind = INSN_RETURN;
        return 0;
    }
    if (!two_byte && (opcode == 0xCC || opcode == 0xCD || opcode == 0xF4)) {
        if (opcode == 0xCD && position >= available) return -1;
        if (opcode == 0xCD) ++position;
        instruction->length = (uint8_t)position;
        instruction->kind = INSN_TRAP;
        return 0;
    }

    unsigned char modrm = 0;
    int has_modrm = opcode_needs_modrm(two_byte ? second_opcode : opcode, two_byte);
    if (has_modrm) {
        if (position >= available) return -1;
        modrm = bytes[position];
        size_t consumed = modrm_length(bytes, available, position, modrm);
        if (consumed == 0) return -1;
        position += consumed;
        if (!two_byte && opcode == 0xFF && ((modrm >> 3) & 7) == 4) {
            instruction->kind = INSN_JUMP;
        } else if (!two_byte && opcode == 0xFF && ((modrm >> 3) & 7) == 2) {
            instruction->kind = INSN_CALL;
        }
    }
    unsigned char immediate_size = 0;
    unsigned char selected_opcode = two_byte ? second_opcode : opcode;
    if (!two_byte) {
        if (opcode >= 0xB0 && opcode <= 0xB7) immediate_size = 1;
        else if (opcode >= 0xB8 && opcode <= 0xBF) immediate_size = rex_w ? 8 : 4;
        else if (opcode == 0x68) immediate_size = 4;
        else if (opcode == 0x6A) immediate_size = 1;
        else if (opcode == 0x80 || opcode == 0x82 || opcode == 0x83 || opcode == 0xC0 ||
                 opcode == 0xC1 || opcode == 0xC6 || opcode == 0x6B) immediate_size = 1;
        else if (opcode == 0x81 || opcode == 0xC7 || opcode == 0x69) immediate_size = 4;
        else if ((opcode == 0xF6 && ((modrm >> 3) & 7) == 0)) immediate_size = 1;
        else if ((opcode == 0xF7 && ((modrm >> 3) & 7) == 0)) immediate_size = 4;
    } else if (selected_opcode == 0xBA || selected_opcode == 0x70 ||
               selected_opcode == 0x71 || selected_opcode == 0x72 ||
               selected_opcode == 0x73) {
        immediate_size = 1;
    }
    if (position + immediate_size > available || position + immediate_size > 15) {
        return -1;
    }
    position += immediate_size;
    instruction->length = (uint8_t)position;
    if (instruction->length == 0) instruction->length = 1;

    const char *mnemonic = NULL;
    if (two_byte) {
        switch (second_opcode) {
        case 0x1E: mnemonic = "endbr64"; break;
        case 0x1F: mnemonic = "nop"; break;
        case 0xAF: mnemonic = "imul"; break;
        case 0xB6: case 0xB7: mnemonic = "movzx"; break;
        case 0xBE: case 0xBF: mnemonic = "movsx"; break;
        case 0x05: mnemonic = "syscall"; break;
        default: break;
        }
    } else {
        if (opcode >= 0x50 && opcode <= 0x57) mnemonic = "push";
        else if (opcode >= 0x58 && opcode <= 0x5F) mnemonic = "pop";
        else if (opcode >= 0xB0 && opcode <= 0xBF) mnemonic = "mov";
        else switch (opcode) {
        case 0x90: mnemonic = "nop"; break;
        case 0x89: case 0x8B: case 0xC6: case 0xC7: mnemonic = "mov"; break;
        case 0x8D: mnemonic = "lea"; break;
        case 0x01: case 0x03: case 0x80: case 0x81: case 0x82: case 0x83:
            mnemonic = "alu"; break;
        case 0x29: case 0x2B: mnemonic = "sub"; break;
        case 0x31: case 0x33: mnemonic = "xor"; break;
        case 0x39: case 0x3B: mnemonic = "cmp"; break;
        case 0x84: case 0x85: mnemonic = "test"; break;
        case 0xC9: mnemonic = "leave"; break;
        case 0xF8: mnemonic = "clc"; break;
        case 0xF9: mnemonic = "stc"; break;
        case 0xFC: mnemonic = "cld"; break;
        case 0xFD: mnemonic = "std"; break;
        default: break;
        }
    }
    if (mnemonic != NULL) snprintf(instruction->mnemonic, sizeof(instruction->mnemonic), "%s", mnemonic);
    return 0;
}

static int find_instruction_at(const Instruction *instructions, int count, uint64_t address)
{
    for (int i = 0; i < count; ++i) {
        if (instructions[i].address == address) return i;
    }
    return -1;
}

static void add_successor(BasicBlock *block, int successor)
{
    if (successor < 0 || block->successor_count >= 2) return;
    for (int i = 0; i < block->successor_count; ++i) {
        if (block->successors[i] == successor) return;
    }
    block->successors[block->successor_count++] = successor;
}

void free_cfg(ControlFlowGraph *cfg)
{
    free(cfg->instructions);
    free(cfg->blocks);
    memset(cfg, 0, sizeof(*cfg));
}

int build_cfg(const ElfImage *image, const Function *function,
              ControlFlowGraph *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    if (function->size > 64U * 1024U * 1024U ||
        !range_valid(image, function->file_offset, function->size)) return -1;
    int capacity = function->size > 0x7fffffffU ? 0x7fffffff : (int)function->size;
    if (capacity <= 0) return -1;
    cfg->instructions = (Instruction *)calloc((size_t)capacity, sizeof(Instruction));
    if (cfg->instructions == NULL) return -1;
    const unsigned char *code = image->bytes + function->file_offset;
    size_t offset = 0;
    while (offset < function->size && cfg->instruction_count < capacity) {
        Instruction *instruction = &cfg->instructions[cfg->instruction_count];
        if (decode_instruction(code + offset, (size_t)function->size - offset,
                               function->address + offset, instruction) != 0 ||
            instruction->length == 0) {
            instruction->length = 1;
            instruction->kind = INSN_NORMAL;
            instruction->target = 0;
            instruction->opcode = code[offset];
        }
        instruction->offset = (uint32_t)offset;
        instruction->address = function->address + offset;
        ++cfg->instruction_count;
        offset += instruction->length;
    }
    if (cfg->instruction_count == 0) {
        free_cfg(cfg);
        return -1;
    }
    unsigned char *starts = (unsigned char *)calloc((size_t)cfg->instruction_count, 1);
    int *block_for_instruction = (int *)malloc((size_t)cfg->instruction_count * sizeof(int));
    if (starts == NULL || block_for_instruction == NULL) {
        free(starts); free(block_for_instruction); free_cfg(cfg); return -1;
    }
    starts[0] = 1;
    for (int i = 0; i < cfg->instruction_count; ++i) {
        const Instruction *instruction = &cfg->instructions[i];
        if (instruction->kind == INSN_CONDITIONAL || instruction->kind == INSN_JUMP) {
            int target_index = find_instruction_at(cfg->instructions,
                                                   cfg->instruction_count,
                                                   instruction->target);
            if (target_index >= 0) starts[target_index] = 1;
            if (instruction->kind == INSN_CONDITIONAL && i + 1 < cfg->instruction_count) {
                starts[i + 1] = 1;
            }
        }
        if ((instruction->kind == INSN_RETURN || instruction->kind == INSN_TRAP) &&
            i + 1 < cfg->instruction_count) starts[i + 1] = 1;
    }
    int blocks = 0;
    for (int i = 0; i < cfg->instruction_count; ++i) if (starts[i]) ++blocks;
    cfg->blocks = (BasicBlock *)calloc((size_t)blocks, sizeof(BasicBlock));
    if (cfg->blocks == NULL) {
        free(starts); free(block_for_instruction); free_cfg(cfg); return -1;
    }
    cfg->block_count = blocks;
    int current_block = -1;
    for (int i = 0; i < cfg->instruction_count; ++i) {
        if (starts[i]) {
            ++current_block;
            cfg->blocks[current_block].first_instruction = i;
            cfg->blocks[current_block].last_instruction = i;
            cfg->blocks[current_block].successor_count = 0;
        } else {
            cfg->blocks[current_block].last_instruction = i;
        }
        block_for_instruction[i] = current_block;
    }
    for (int b = 0; b < cfg->block_count; ++b) {
        int last_index = cfg->blocks[b].last_instruction;
        const Instruction *last = &cfg->instructions[last_index];
        if (last->kind == INSN_CONDITIONAL) {
            int target_index = find_instruction_at(cfg->instructions,
                                                   cfg->instruction_count,
                                                   last->target);
            if (target_index >= 0) {
                add_successor(&cfg->blocks[b], block_for_instruction[target_index]);
            }
            if (last_index + 1 < cfg->instruction_count) {
                add_successor(&cfg->blocks[b], block_for_instruction[last_index + 1]);
            }
        } else if (last->kind == INSN_JUMP) {
            int target_index = find_instruction_at(cfg->instructions,
                                                   cfg->instruction_count, last->target);
            if (target_index >= 0) add_successor(&cfg->blocks[b], block_for_instruction[target_index]);
        } else if (last->kind != INSN_RETURN && last->kind != INSN_TRAP &&
                   last_index + 1 < cfg->instruction_count) {
            add_successor(&cfg->blocks[b], block_for_instruction[last_index + 1]);
        }
    }
    free(starts);
    free(block_for_instruction);
    return 0;
}

static void instruction_text(const Instruction *instruction, char *buffer, size_t size)
{
    switch (instruction->kind) {
    case INSN_CONDITIONAL:
        snprintf(buffer, size, "jcc 0x%llx", (unsigned long long)instruction->target);
        break;
    case INSN_JUMP:
        if (instruction->target != 0)
            snprintf(buffer, size, "jmp 0x%llx", (unsigned long long)instruction->target);
        else
            snprintf(buffer, size, "jmp *indirect");
        break;
    case INSN_CALL:
        if (instruction->target != 0)
            snprintf(buffer, size, "call 0x%llx", (unsigned long long)instruction->target);
        else
            snprintf(buffer, size, "call *indirect");
        break;
    case INSN_RETURN:
        snprintf(buffer, size, "ret");
        break;
    case INSN_TRAP:
        snprintf(buffer, size, "trap");
        break;
    default:
        if (instruction->mnemonic[0] != '\0')
            snprintf(buffer, size, "%s", instruction->mnemonic);
        else
            snprintf(buffer, size, "db 0x%02x", instruction->opcode);
        break;
    }
}

static void safe_name(const char *source, char *destination, size_t size)
{
    size_t out = 0;
    if (size == 0) return;
    for (size_t i = 0; source[i] != '\0' && out + 1 < size; ++i) {
        unsigned char ch = (unsigned char)source[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.') {
            destination[out++] = (char)ch;
        } else {
            destination[out++] = '_';
        }
    }
    if (out == 0) destination[out++] = 'f';
    destination[out] = '\0';
}

int write_graph(const char *directory, const Function *function,
                const ControlFlowGraph *cfg, int graph_id)
{
    char display_name[96];
    safe_name(function->name, display_name, sizeof(display_name));
    char path[PATH_MAX];
    int path_length = snprintf(path, sizeof(path), "%s/function_%04d.dot",
                               directory, graph_id);
    if (path_length < 0 || (size_t)path_length >= sizeof(path)) {
        fprintf(stderr, "output path is too long for %s\n", function->name);
        return -1;
    }
    /* Do not silently overwrite an output left by an earlier run. */
    if (access(path, F_OK) == 0) {
        fprintf(stderr, "output already exists: %s\n", path);
        return -1;
    }
    sched_yield();
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        fprintf(stderr, "cannot write %s: %s\n", path, strerror(errno));
        return -1;
    }
    fprintf(file, "digraph cfg_%d {\n", graph_id);
    fprintf(file, "  graph [label=\"%s\", labelloc=t];\n", display_name);
    for (int b = 0; b < cfg->block_count; ++b) {
        fprintf(file, "  bb%d [shape=box, label=\"BB%d\\l", b, b);
        for (int i = cfg->blocks[b].first_instruction;
             i <= cfg->blocks[b].last_instruction; ++i) {
            char text[96];
            instruction_text(&cfg->instructions[i], text, sizeof(text));
            fprintf(file, "0x%llx: %s\\l",
                    (unsigned long long)cfg->instructions[i].address, text);
        }
        fprintf(file, "\"];\n");
    }
    for (int b = 0; b < cfg->block_count; ++b) {
        for (int s = 0; s < cfg->blocks[b].successor_count; ++s) {
            fprintf(file, "  bb%d -> bb%d;\n", b, cfg->blocks[b].successors[s]);
        }
    }
    fprintf(file, "}\n");
    int close_result = fclose(file);
    if (close_result != 0) {
        fprintf(stderr, "cannot close %s: %s\n", path, strerror(errno));
        return -1;
    }
    return 0;
}

int next_graph_id(void)
{
    int id = g_next_graph_id;
    sched_yield();
    g_next_graph_id = id + 1;
    return id;
}

void add_statistics(WorkState *state, unsigned long instructions,
                    unsigned long blocks)
{
    pthread_mutex_lock(&state->stats_mutex);
    g_total_instructions += instructions;
    g_total_blocks += blocks;
    pthread_mutex_unlock(&state->stats_mutex);
}

void *worker_main(void *argument)
{
    WorkState *state = (WorkState *)argument;
    for (;;) {
        pthread_mutex_lock(&state->job_mutex);
        int job = state->next_job++;
        pthread_mutex_unlock(&state->job_mutex);
        if (job >= state->function_count) break;

        const Function *function = &state->functions[job];
        ControlFlowGraph cfg;
        if (build_cfg(state->image, function, &cfg) != 0) {
            fprintf(stderr, "could not build CFG for %s\n", function->name);
            continue;
        }
        int graph_id = next_graph_id();
        if (write_graph(state->output_directory, function, &cfg, graph_id) != 0) {
            fprintf(stderr, "graph generation failed for %s\n", function->name);
        }
        add_statistics(state, (unsigned long)cfg.instruction_count,
                       (unsigned long)cfg.block_count);
        free_cfg(&cfg);
    }
    return NULL;
}
