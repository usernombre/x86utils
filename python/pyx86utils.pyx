from libc.stdlib cimport malloc, free

cdef extern from "Python.h":
    object PyString_FromStringAndSize(char *s, Py_ssize_t len)
    object PyString_FromString(char *s)
    
cdef extern from "stdint.h":
    ctypedef signed char 	int8_t
    ctypedef unsigned char 	uint8_t
    ctypedef signed int 	int16_t
    ctypedef unsigned int 	uint16_t
    ctypedef signed long int 	int32_t
    ctypedef unsigned long int 	uint32_t
    ctypedef signed long long int 	int64_t
    ctypedef unsigned long long int 	uint64_t

cdef extern from "x86utils.h":
    # Assembler
    cdef struct assembler:
        pass
    ctypedef assembler assembler_t
    assembler_t * assembler_create(int mode)
    void assembler_destroy(assembler_t * asmb)
    int assembler_assemble(assembler_t * asmb, const char * assembly, uint64_t address);
    int assembler_result(assembler_t * asmb, unsigned char * output, size_t * output_size);
    int assembler_relocs(assembler_t * asmb, unsigned char * output, size_t * output_size);
    
    # Instruction
    cdef enum ud_mnemonic_code:
        pass
    ctypedef ud_mnemonic_code ud_mnemonic_code_t
    
    cdef enum ud_type:
        pass
    ctypedef ud_type ud_type_t
    
    cpdef enum operand_type:
        NONE = 0x0
        OP_REG = 0x10
        OP_MEM = 0x20
        OP_MEM_8 = 0x21
        OP_MEM_16 = 0x22
        OP_MEM_32 = 0x23
        OP_MEM_64 = 0x24
        OP_IMM = 0x30
        OP_IMM_8 = 0x31
        OP_IMM_16 = 0x32
        OP_IMM_32 = 0x33
        OP_IMM_64 = 0x34
        OP_PTR = 0x40
        OP_JIMM = 0x50
    ctypedef operand_type operand_type_t
    
    cdef struct operand_info:
        operand_type_t type
        uint8_t size
        ud_type_t reg
        uint64_t value
        ud_type_t segment
        ud_type_t base
        ud_type_t index
        uint8_t scale
        int64_t offset
        uint16_t ptr_segment
        uint32_t ptr_offset
    ctypedef operand_info operand_info_t
    
    cdef struct instruction_info:
        ud_mnemonic_code_t opcode
        uint8_t size
        uint8_t near
        uint8_t far
        operand_info_t operands[3]
    ctypedef instruction_info instruction_info_t
    
    const char * opcode_string(ud_mnemonic_code_t opcode);
    const char * register_string(ud_type_t reg);

    instruction_info_t * instruction_create(int mode);
    void instruction_destroy(instruction_info_t * inst);

    int instruction_disassemble(instruction_info_t * inst, unsigned char * input_buffer, uint32_t input_size, uint64_t address);
    void operand_string(operand_info_t * op, char * output, int output_size)
    void instruction_prefix_string(instruction_info_t * inst, char * output, int output_size)
    void instruction_string(instruction_info_t * inst, char * output, int output_size);
    int instruction_assemble(instruction_info_t * inst, unsigned char * output_buffer, size_t * output_size, uint64_t address);

OP_NONE = NONE

def _str_to_none(s):
    if len(s):
        return s
    return None
    
cdef class Operand:
    cdef object instruction
    cdef operand_info_t * operand
    
            
    property type:
        def __get__(self):
            return self.operand.type
    property size:
        def __get__(self):
            return self.operand.size >> 3
    property reg:
        def __get__(self):
            return _str_to_none(PyString_FromString(register_string(self.operand.reg)))
    property value:
        def __get__(self):
            return self.operand.value
    property segment:
        def __get__(self):
            return _str_to_none(PyString_FromString(register_string(self.operand.segment)))
    property base:
        def __get__(self):
            return _str_to_none(PyString_FromString(register_string(self.operand.base)))
    property index:
        def __get__(self):
            return _str_to_none(PyString_FromString(register_string(self.operand.index)))
    property scale:
        def __get__(self):
            return self.operand.scale
    property offset:
        def __get__(self):
            return self.operand.offset
    property ptr_segment:
        def __get__(self):
            return self.operand.ptr_segment
    property ptr_offset:
        def __get__(self):
            return self.operand.ptr_offset
    
    def __init__(self, instruction):
        self.instruction = instruction # just keep reference
            
    def __str__(self):
        cdef char operand[100]
        operand[0] = 0
        operand_string(self.operand, operand, sizeof(operand))
        return PyString_FromString(operand)
        
    def is_reg(self, reg = None):
        if self.type != OP_REG:
            return False
        if reg:
            return self.reg == reg
        return True

    def is_immediate(self, value = None):
        if (self.type & ~0xf) not in (OP_IMM, OP_JIMM):
            return False
        if value != None:
            return self.value == (value % (1<<64))
        return True

    def is_memory(self):
        return (self.type & ~0xf) == OP_MEM
        
cdef class Instruction:
    cdef instruction_info_t * instruction
    cdef public object address
    cdef public object next
    
    property opcode:
        def __get__(self):
            return PyString_FromString(opcode_string(self.instruction.opcode))
            
    property prefix:
        def __get__(self):
            cdef char prefix[100]
            prefix[0] = 0
            instruction_prefix_string(self.instruction, prefix, sizeof(prefix))
            return PyString_FromString(prefix)[:-1] # The last character is space
            
    property size:
        def __get__(self):
            return self.instruction.size
            
    property jump_type:
        def __get__(self):
            if self.instruction.near:
                return "near"
            if self.instruction.near:
                return "far"
            return ""
            
    property operands:
        def __get__(self):
            # TODO: Add __len__
            class Operands:
                def __getitem__(s, i):
                    if not (0 <= i < 3):
                        return None
                    if self.instruction.operands[i].type == OP_NONE: 
                        return None
                    op = Operand(self)
                    op.operand = &self.instruction.operands[i]
                    return op
            return Operands()
    
    def __cinit__(self):
        self.instruction = NULL
        
    def __init__(self, data, address = 0, mode = 32, raise_if_illegal =  True):
        self.instruction = instruction_create(mode) 
        self.address = address
        if self.instruction is NULL:
            raise MemoryError()
            
        if instruction_disassemble(self.instruction, data, len(data), address) != 0:
            if raise_if_illegal:
                raise Exception("Invalid instruction")
            
        self.next = self.address + self.size
            
    def __str__(self):
        cdef char instruction[100]
        instruction[0] = 0
        instruction_string(self.instruction, instruction, sizeof(instruction))
        return PyString_FromString(instruction)

    def __repr__(self):
        return "<Instruction '%s' >" % str(self)
        
    def __dealloc__(self):
        if self.instruction is not NULL:
            instruction_destroy(self.instruction)


cdef class Assembler:
    cdef assembler_t * assembler
    
    def __cinit__(self):
        self.assembler = NULL
    
    def __init__(self, mode = 32):
        self.assembler = assembler_create(mode)
        if self.assembler is NULL:
            raise MemoryError()
        
    def assemble(self, str, address = 0, return_relocs = False):
        cdef size_t output_size
        cdef int *relocs
        if assembler_assemble(self.assembler, str, address) != 0:
            return None
        if assembler_result(self.assembler, <unsigned char *>NULL, &output_size) != 0:    
            return None
        cdef void * output = malloc(output_size)
        try:
            if output is NULL:
                raise MemoryError()
            if assembler_result(self.assembler, <unsigned char *>output, &output_size) != 0:  
                return None
            res = PyString_FromStringAndSize(<char *>output, output_size)
        finally:
            free(output)
        if return_relocs and res:
            if assembler_relocs(self.assembler, <unsigned char *>NULL, &output_size) != 0:    
                return None
            if (output_size & 3) != 0: # Should be a multiply of 4
                raise Exception("Invalid relocs size")
            relocs = <int *>malloc(output_size)
            try:
                if relocs is NULL:
                    raise MemoryError()
                if assembler_relocs(self.assembler, <unsigned char *>relocs, &output_size) != 0:  
                    return None
                return res,  [relocs[i] for i in range(output_size >> 2)]
            finally:
                free(relocs)
        return res
        
        
    def __dealloc__(self):
        if self.assembler is not NULL:
            assembler_destroy(self.assembler)
            