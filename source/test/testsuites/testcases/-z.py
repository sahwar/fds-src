__author__ = 'pooja'
import sys

# Where is '-q'? We know it's there or we wouldn't be here.
idx = sys._getframe(2).f_locals['names'].index('-z')

# Remove '-q' and its argument.
sys._getframe(2).f_locals['names'].pop(sys._getframe(2).f_locals['names'].__len__()-1)
sys._getframe(2).f_locals['names'].pop(sys._getframe(2).f_locals['names'].__len__()-1)

