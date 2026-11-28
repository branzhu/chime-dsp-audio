"""Pack the existing exact 48k Q30 FIR rows into signed16 hi/lo planes.
No filter redesign or quantization. Values reconstruct as hi*65536+lo.
"""
import ast
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
source=root/'software/main/chime_bt_src_coeff.h'
banks=ast.literal_eval(source.read_text().split('=',1)[1].strip().rstrip(';').replace('{','[').replace('}',']'))
rows=banks[2];assert len(rows)==257
lines=['/* Exact signed-half packing of the existing 48k Q30 coefficients. */','_Alignas(16) static const int16_t src_packed_48k[257][2][192]={']
for row in rows:
 assert len(row)==192
 lo=[((c+32768)%65536)-32768 for c in row]
 hi=[(c-l)//65536 for c,l in zip(row,lo)]
 assert all(-32768<=h<=32767 and h*65536+l==c for c,h,l in zip(row,hi,lo))
 lines.append('{{'+','.join(map(str,hi))+'},{'+','.join(map(str,lo))+'}},')
lines.append('};');text='\n'.join(lines)+'\n'
p=root/'software/main/chime_bt_src_packed_48k.h'
if '--check' in sys.argv:assert p.read_text()==text
else:p.write_text(text)
print('PASS exact Q30 signed-half packing: 257 x 2 x 192, 197376 bytes')
