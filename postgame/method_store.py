from copy import copy
from typing import Dict, Optional

from typing_extensions import Self

from postgame.method import Method


class MethodStore:
    def __init__(self):
        self._methods: Dict[int, Method] = dict()  # map from address to method

    def __copy__(self):
        result = MethodStore()
        result._methods = copy(self._methods)
        return result

    def find_or_insert_method(
        self,
        address: int,
        found_dynamically: bool = True,
    ) -> Method:
        """
        Attempts to find the method in the global methods map. If the function fails
        to find a method, one will be inserted.
        """
        if address not in self._methods:
            self._methods[address] = Method(
                address,
                found_dynamically=found_dynamically,
            )
        return self._methods[address]

    def getMethod(self, address: int) -> Optional[Method]:
        return self._methods[address] if address in self._methods else None

    def insertMethodName(self, address: int, name: str):
        meth = self.getMethod(address)
        if meth is not None:
            meth.name = name

    def update(self, other: Self):
        """
        Merge the other store into this one, modifying self in-place.
        """
        self._methods.update(other._methods)

    def reset_all_method_statistics(self):
        for method in self._methods.values():
            method.resetMethodStatistics()
            method.resetMethodStatistics()
