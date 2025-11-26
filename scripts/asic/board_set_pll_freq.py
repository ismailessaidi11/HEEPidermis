import sys
sys.path.append('./cheep_boards_sw_stack/python/')

from cheep import *
from pyftdi.ftdi import Ftdi
from pyftdi.i2c import I2cController, I2cNackError

i2c = I2cController()


url = "ftdi://ftdi:4232h/2"
print(url)

# then use it
i2c.configure(url)
cb = CheepBoard(i2c)
pll_freq = int(sys.argv[1]) if len(sys.argv) > 1 else 1_000_000
cb.pll.set_frequency(pll_freq)
