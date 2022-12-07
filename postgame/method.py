from typing import Callable

class Method:
    def __init__(self, address: int, baseAddr: int, getMethodNameFn: Callable[[int], str]):
        self.address = address
        self.baseAddr = baseAddr
        self.getMethodNameFn = getMethodNameFn
        self.type = ''
        # How many times the method has been seen in different parts of a trace:
        self.resetMethodStatistics()

        self.destructorTailToTorsoRatioMax = 4
        self.constructorHeadToTorsoRatioMax = 4

        self.isInitializer = False
        self.isFinalizer = False

    def resetMethodStatistics(self):
        self.seenInHead = int(0)
        self.seenInFingerprint = int(0)
        self.seenTotal = int(0)

    def seenInTorso(self):
        return self.seenTotal - self.seenInHead - self.seenInFingerprint

    def isInFingerprint(self) -> bool:
        return self.seenInFingerprint > 0

    def isInHead(self) -> bool:
        return self.seenInHead > 0

    def isProbablyConstructor(self) -> bool:
        '''
        Returns true if method believed to be a destructor. While a method is
        likely a constructor if it appears in the head, we make the assumption
        that it cannot be a constructor if it appears in the fingerprint. Also,
        if the method is not found mostly in the head but instead is found
        somewhat regularly in the body of traces, it is likely not a constructor
        either.
        '''
        return self.isInHead() and \
               not self.isInFingerprint() and \
               self.constructorHeadToTorsoRatioMax * self.seenInTorso() <= self.seenInHead

    def isProbablyDestructor(self) -> bool:
        '''
        @see isProbablyConstructor. The same idea here but for destructors.
        '''
        return self.isInFingerprint() and \
               not self.isInHead() and \
               self.destructorTailToTorsoRatioMax * self.seenInTorso() <= self.seenInFingerprint

    def updateType(self) -> None:
        # TODO other types may be viable options (virtual methods for example), but for now we don't care about them
        assert not (self.isProbablyConstructor() and self.isProbablyDestructor())

        if self.isInFingerprint():
            self.type = 'dtor'
        elif self.isInHead():
            self.type = 'ctor'
        else:
            self.type = 'meth'

    def __str__(self) -> str:
        name = self.getMethodNameFn(self)
        return ('' if name == None else (name + ' ')) +\
               (hex(self.address + self.baseAddr)) +\
               ('' if self.type == '' else ' ' + self.type)