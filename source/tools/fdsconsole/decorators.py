
import helpers

def clicmd(func):
    setattr(func, helpers.ATTR_CLICMD, helpers.AccessLevel.USER)
    return func

def cliadmincmd(func):
    setattr(func, helpers.ATTR_CLICMD, helpers.AccessLevel.ADMIN)
    return func

def clidebugcmd(func):
    setattr(func, helpers.ATTR_CLICMD, helpers.AccessLevel.DEBUG)
    return func
