from typing import Dict
from method import Method

class MethodStore:
    def __init__(self):
        self._methods: Dict[int, Method] = dict()  # map from address to method
        self._methodNames: Dict[Method, str] = dict()  # map from method to method name

    def findOrInsertMethod(self, address: int, baseAddr: int) -> Method:
        '''
        Attempts to find the method in the global methods map. If the function fails
        to find a method, one will be inserted.
        '''
        if address not in self._methods:
            self._methods[address] = Method(address, baseAddr, self.getMethodName)
        return self._methods[address]

    def insertMethodName(self, methodAddress: int, name: str) -> None:
        '''
        Inserts the method in the methodNames map. Method name will only be inserted
        if there exists a method in the methods map with the methodAddress given.
        Therefore, method names should be inserted after the methods map is finalized.
        '''
        if methodAddress in self._methods:
            self._methodNames[self._methods[methodAddress]] = name

    def getMethodName(self, method: Method) -> str:
        '''
        Finds and return the method name for the given method. Returns None if method name not found.
        '''
        return self._methodNames.get(method, None)

    def resetAllMethodStatistics(self):
        for method in self._methods.values():
            method.resetMethodStatistics()
