"""Minimal R5900 listing for hand review: ps2_dis.py ELF start end. Decodes the common integer/branch/load-store ops; others print raw."""
import struct, sys
R=['zero','at','v0','v1','a0','a1','a2','a3','t0','t1','t2','t3','t4','t5','t6','t7','s0','s1','s2','s3','s4','s5','s6','s7','t8','t9','k0','k1','gp','sp','fp','ra']
elf=open(sys.argv[1],'rb').read();phoff,=struct.unpack_from('<I',elf,0x1C);_,off,va,_,fsz,_,_,_=struct.unpack_from('<IIIIIIII',elf,phoff)
a,b=int(sys.argv[2],0),int(sys.argv[3],0)
ops={0x04:'beq',0x05:'bne',0x06:'blez',0x07:'bgtz',0x14:'beql',0x15:'bnel',0x08:'addi',0x09:'addiu',0x0A:'slti',0x0B:'sltiu',0x0C:'andi',0x0D:'ori',0x0E:'xori',0x0F:'lui',0x19:'daddiu',
0x20:'lb',0x21:'lh',0x23:'lw',0x24:'lbu',0x25:'lhu',0x27:'lwu',0x28:'sb',0x29:'sh',0x2B:'sw',0x37:'ld',0x3F:'sd',0x1E:'lq',0x1F:'sq',0x31:'lwc1',0x39:'swc1'}
fn={0:'sll',2:'srl',3:'sra',8:'jr',9:'jalr',0x21:'addu',0x23:'subu',0x24:'and',0x25:'or',0x26:'xor',0x27:'nor',0x2A:'slt',0x2B:'sltu',0x2D:'daddu',0x0A:'movz',0x0B:'movn',0x04:'sllv',0x38:'dsll',0x3C:'dsll32',0x3F:'dsra32',0x3A:'dsrl',0x3E:'dsrl32'}
for ad in range(a,b,4):
    w,=struct.unpack_from('<I',elf,off+ad-va);op=w>>26;rs=(w>>21)&31;rt=(w>>16)&31;rd=(w>>11)&31;imm=w&0xFFFF;s=imm-0x10000 if imm&0x8000 else imm
    if w==0:t='nop'
    elif op==0:t='%s %s,%s,%s sa=%d'%(fn.get(w&63,'func%#x'%(w&63)),R[rd],R[rs],R[rt],(w>>6)&31)
    elif op==2 or op==3:t='%s %#x'%('j' if op==2 else 'jal',((w&0x3FFFFFF)<<2)|(ad&0xF0000000))
    elif op in(4,5,6,7,0x14,0x15):t='%s %s,%s -> %#x'%(ops[op],R[rs],R[rt],ad+4+4*s)
    elif op==1:t='regimm rt=%d %s -> %#x'%(rt,R[rs],ad+4+4*s)
    elif op in ops:t='%s %s,%s,%#x'%(ops[op],R[rt],R[rs],s) if op not in(0x20,0x21,0x23,0x24,0x25,0x27,0x28,0x29,0x2B,0x37,0x3F,0x1E,0x1F,0x31,0x39) else '%s %s,%#x(%s)'%(ops[op],R[rt],s,R[rs])
    else:t='raw %08x'%w
    print('%#x: %s'%(ad,t))
