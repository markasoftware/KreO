from dataclasses import dataclass

from postgame.analysis_results import MethodType


@dataclass
class Method:
    address: int
    found_dynamically: bool = True
    name: str = ""

    type: MethodType = MethodType.meth
    is_initializer: bool = False
    is_finalizer: bool = False

    seen_in_head: int = 0
    seen_in_tail: int = 0
    seen_count: int = 0

    def reset_method_statistics(self):
        self.seen_in_head = 0
        self.seen_in_tail = 0
        self.seen_count = 0

    def update_type(self):
        """Update method type based on method statistics."""
        if self.seen_in_head != 0 and self.seen_in_tail != 0:
            msg = "failed to update method type. method is seen in both the head and tail."
            raise RuntimeError(msg)

        if self.seen_in_head > 0:
            self.type = MethodType.ctor
        elif self.seen_in_tail > 0:
            self.type = MethodType.dtor
        else:
            self.type = MethodType.meth

    def __str__(self) -> str:
        return hex(self.address)[2:]

    def __hash__(self) -> int:
        return self.address
