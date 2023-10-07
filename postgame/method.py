from dataclasses import dataclass
from enum import StrEnum, auto


class Type(StrEnum):
    dtor = auto()
    ctor = auto()
    meth = auto()


@dataclass
class Method:
    address: int
    found_dynamically: bool = True
    name: str = ""

    type: Type = Type.meth
    is_initializer: bool = False
    is_finalizer: bool = False

    seen_in_head: int = 0
    seen_in_tail: int = 0
    seen_count: int = 0

    def reset_method_statistics(self):
        self.seen_in_head = 0
        self.seen_in_tail = 0
        self.seen_count = 0

    def isInHead(self) -> bool:
        return self.seen_in_head > 0

    def isInTail(self) -> bool:
        return self.seen_in_tail > 0

    # def isProbablyConstructor(self) -> bool:
    #     """
    #     Returns true if method believed to be a destructor. While a method is
    #     likely a constructor if it appears in the head, we make the assumption
    #     that it cannot be a constructor if it appears in the fingerprint. Also,
    #     if the method is not found mostly in the head but instead is found
    #     somewhat regularly in the body of traces, it is likely not a constructor
    #     either.
    #     """
    #     return (
    #         self.isInHead()
    #         and not self.isInTail()
    #         and self._ctor_head_to_torso_ratio_max * self.seen_in_torso
    #         <= self.seen_in_head
    #     )

    # def is_probably_destructor(self) -> bool:
    #     """
    #     @see isProbablyConstructor. The same idea here but for destructors.
    #     """
    #     return (
    #         self.isInTail()
    #         and not self.isInHead()
    #         and self._dtor_tail_to_torso_ratio_max * self.seen_in_torso
    #         <= self.seen_in_tail
    #     )

    # def update_type(self) -> None:
    #     # TODO other types may be viable options (virtual methods for example), but for
    #     # now we don't care about them
    #     if self.is_probably_destructor():
    #         self.type = "dtor"
    #     elif self.isProbablyConstructor():
    #         self.type = "ctor"
    #     else:
    #         self.type = "meth"

    def __str__(self) -> str:
        return hex(self.address)[2:]

    def __hash__(self) -> int:
        return self.address
