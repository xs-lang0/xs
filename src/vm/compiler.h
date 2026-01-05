#ifndef VM_COMPILER_H
#define VM_COMPILER_H
#include "core/ast.h"
#include "vm/bytecode.h"

XSProto *compile_program(Node *program);

#endif
