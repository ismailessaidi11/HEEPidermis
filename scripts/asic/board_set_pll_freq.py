import sys
sys.path.append('./cheep_boards_sw_stack/python/')

from cheep import *
from pyftdi.ftdi import Ftdi
from pyftdi.i2c import I2cController, I2cNackError

i2c = I2cController()


import subprocess
import re

# run the script and capture output
out = subprocess.check_output(
    ["python3", "/home/juan/.local/bin/ftdi_urls.py"],
    text=True
)

# find the line that matches ftdi://.../2
m = re.search(r"(ftdi://ftdi:[0-9a-fA-F:]+/2)", out)
url = m.group(1) if m else None

print(url)

# then use it
i2c.configure(url)
cb = CheepBoard(i2c)
pll_freq = int(sys.argv[1]) if len(sys.argv) > 1 else 1_000_000
cb.pll.set_frequency(pll_freq)
