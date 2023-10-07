from copy import copy
from typing import Dict, Optional

from typing_extensions import Self

from postgame.method import Method


class MethodStore:
    def __init__(self):
        self.__methods: Dict[int, Method] = dict()  # map from address to method

    def __copy__(self):
        result = MethodStore()
        result.__methods = copy(self.__methods)
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
        if address not in self.__methods:
            self.__methods[address] = Method(
                address,
                found_dynamically=found_dynamically,
            )
        return self.__methods[address]

    def get_method(self, address: int) -> Optional[Method]:
        return self.__methods[address] if address in self.__methods else None

    def insert_method_name(self, address: int, name: str):
        meth = self.get_method(address)
        if meth is not None:
            meth.name = name

    def update(self, other: Self):
        """
        Merge the other store into this one, modifying self in-place.
        """
        self.__methods.update(other.__methods)

    def reset_all_method_statistics(self):
        for method in self.__methods.values():
            method.reset_method_statistics()
            method.reset_method_statistics()
