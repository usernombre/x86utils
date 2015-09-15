from _pyx86utils import ffi, lib

def _str_to_none(s):
    if len(s):
        return s
    return None
    
class Operand(object):        
    @property
    def type(self):
        return self.operand.type
    @property
    def size(self):
        return self.operand.size >> 3
    @property
    def reg(self):
        return _str_to_none(ffi.string(lib.register_string(self.operand.reg)))
    @property
    def value(self):
        return self.operand.value
    @property
    def segment(self):
        return _str_to_none(ffi.string(lib.register_string(self.operand.segment)))
    @property
    def base(self):
        return _str_to_none(ffi.string(lib.register_string(self.operand.base)))
    @property
    def index(self):
        return _str_to_none(ffi.string(lib.register_string(self.operand.index)))
    @property
    def scale(self):
        return self.operand.scale
    @property
    def offset(self):
        return self.operand.offset
    @property
    def ptr_segment(self):
        return self.operand.ptr_segment
    @property
    def ptr_offset(self):
        return self.operand.ptr_offset
    
    def __init__(self, operand):
        self.operand = operand

    def __repr__(self):
        return "<Operand '%s' >" % str(self)
            
    def __str__(self):
        operand = ffi.new("char[100]")
        operand[0] = '\0'
        lib.operand_string(ffi.addressof(self.operand), operand, 100)
        return ffi.string(operand)
        
    def is_reg(self, reg = None):
        if self.type != lib.OP_REG:
            return False
        if reg:
            return self.reg == reg
        return True

    def is_immediate(self, value = None):
        if (self.type & ~0xf) not in (lib.OP_IMM, lib.OP_JIMM):
            return False
        if value != None:
            return self.value == (value % (1<<64))
        return True

    def is_memory(self):
        return (self.type & ~0xf) == lib.OP_MEM

class Instruction(object):
    @property
    def opcode(self):
        return ffi.string(lib.opcode_string(self.instruction.opcode))

    @property
    def prefix(self):
        prefix = ffi.new("char[100]")
        prefix[0] = '\0'
        lib.instruction_prefix_string(self.instruction, prefix, 100)
        return ffi.string(prefix)[:-1] # The last character is space

    @property
    def size(self):
        return self.instruction.size
            
    @property
    def jump_type(self):
        if self.instruction.near:
            return "near"
        if self.instruction.near:
            return "far"
 
    def __init__(self, data, address = 0, mode = 32, raise_if_illegal =  True):
        self.instruction = lib.instruction_create(mode) 
        self.address = address
        if self.instruction == ffi.NULL:
            raise MemoryError()

        if lib.instruction_disassemble(self.instruction, data, len(data), address) != 0:
            if raise_if_illegal:
                raise Exception("Invalid instruction")

        self.next = self.address + self.size
        self.operands = [Operand(self.instruction.operands[i]) if self.instruction.operands[i].type != lib.NONE else None for i in xrange(3)]

    def __str__(self):
        instruction = ffi.new("char[100]")
        instruction[0] = '\0'
        lib.instruction_string(self.instruction, instruction, 100)
        return ffi.string(instruction)

    def __repr__(self):
        return "<Instruction '%s' >" % str(self)

    def __del__(self):
        if self.instruction != ffi.NULL:
            lib.instruction_destroy(self.instruction)

class Assembler(object):
    def __init__(self, mode = 32):
        self.assembler = lib.assembler_create(mode)
        if self.assembler == ffi.NULL:
            raise MemoryError()

    def assemble(self, str, address = 0, return_relocs = False):
        output_size = ffi.new("size_t *")
        if lib.assembler_assemble(self.assembler, str, address) != 0:
            return None
        if lib.assembler_result(self.assembler, ffi.NULL, output_size) != 0:    
            return None
        output = ffi.new("unsigned char[]", output_size[0])
        if lib.assembler_result(self.assembler, output, output_size) != 0:  
            return None
        res = ffi.buffer(output, output_size[0])[:]
        if return_relocs and res:
            if lib.assembler_relocs(self.assembler, ffi.NULL, output_size) != 0:    
                return None
            if (output_size[0] & 3) != 0: # Should be a multiply of 4
                raise Exception("Invalid relocs size")
            relocs = ffi.new("int[]", output_size[0] >> 2)
            if lib.assembler_relocs(self.assembler, ffi.cast("char*", relocs), output_size) != 0:  
                return None
            return res, list(relocs)
        return res

    def __del__(self):
        if self.assembler != ffi.NULL:
            lib.assembler_destroy(self.assembler)
            