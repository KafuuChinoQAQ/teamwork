/* Parallel ELF control-flow graph generator. */

#ifndef CFGTOOL_H
#define CFGTOOL_H

#include <elf.h>
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>

enum InstructionKind {
    INSN_NORMAL = 0,
    INSN_CONDITIONAL,
    INSN_JUMP,
    INSN_CALL,
    INSN_RETURN,
    INSN_TRAP
};

struct ElfImage {
    const unsigned char *bytes;
    size_t size;
    int fd;
    uint64_t text_file_offset;
    uint64_t text_address;
    uint64_t text_size;
};

struct Function {
    char *name;
    uint64_t address;
    uint64_t file_offset;
    uint64_t size;
};

struct Instruction {
    uint64_t address;
    uint32_t offset;
    uint8_t length;
    InstructionKind kind;
    uint64_t target;
    unsigned char opcode;
    char mnemonic[16];
};

struct BasicBlock {
    int first_instruction;
    int last_instruction;
    int successor_count;
    int successors[2];
};

struct ControlFlowGraph {
    Instruction *instructions;
    int instruction_count;
    BasicBlock *blocks;
    int block_count;
};

struct WorkState {
    const ElfImage *image;
    Function *functions;
    int function_count;
    const char *output_directory;
    int next_job;
    pthread_mutex_t job_mutex;
    pthread_mutex_t stats_mutex;
};

/* elf.cpp */
int range_valid(const ElfImage *image, uint64_t offset, uint64_t length);
int map_image(const char *path, ElfImage *image);
void unmap_image(ElfImage *image);
int collect_functions(const ElfImage *image, Function **result, int *result_count);
void free_functions(Function *functions, int count);

/* cfg.cpp */
int build_cfg(const ElfImage *image, const Function *function,
              ControlFlowGraph *cfg);
void free_cfg(ControlFlowGraph *cfg);
int write_graph(const char *directory, const Function *function,
                const ControlFlowGraph *cfg, int graph_id);
int next_graph_id(void);
void add_statistics(WorkState *state, unsigned long instructions,
                    unsigned long blocks);
void *worker_main(void *argument);

extern unsigned long g_total_instructions;
extern unsigned long g_total_blocks;

#endif
