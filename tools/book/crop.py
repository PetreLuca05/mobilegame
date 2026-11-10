"""crop.py PDFPAGE x0 y0 x1 y1 OUT [dpi]  -- crop a region (PDF points) at high resolution"""
import sys, subprocess, os
p, x0, y0, x1, y1, out = sys.argv[1], *[float(v) for v in sys.argv[2:6]], sys.argv[6]
dpi = int(sys.argv[7]) if len(sys.argv) > 7 else 250
s = dpi / 72.0
subprocess.run(['pdftoppm', '-r', str(dpi), '-f', p, '-l', p, '-x', str(int(x0 * s)), '-y', str(int(y0 * s)),
                '-W', str(int((x1 - x0) * s)), '-H', str(int((y1 - y0) * s)), '-png', '-singlefile',
                os.environ.get('BOOK_PDF', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'Gastro.pdf')), out[:-4]], check=True)
