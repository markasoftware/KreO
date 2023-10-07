"""Pydantic models representing for parsing analysis results."""

from __future__ import annotations

import itertools

from pydantic import BaseModel, Field


class Member(BaseModel):
    base: bool = False
    name: str
    offset: str = "0x0"
    parent: bool = True
    size: int = 4
    struc: str
    type: str = "struc"
    usages: list[str] = Field(default_factory=list)


class Method(BaseModel):
    demangled_name: str
    ea: str
    imprt: bool = False
    name: str
    type: str


class Structure(BaseModel):
    demangled_name: str = ""
    name: str
    members: dict[str, Member] = Field(default_factory=dict)
    methods: dict[str, Method] = Field(default_factory=dict)
    size: int = 0
    vftables: list[str] = Field(default_factory=list)


class AnalysisResults(BaseModel):
    filename: str
    filemd5: str
    structures: dict[str, Structure] = Field(default_factory=dict)
    vcalls: dict[str, str] = Field(default_factory=dict)
    version: str

    def get_methods(self) -> list[Method]:
        meths = [x.methods.values() for x in self.structures.values()]
        return list(itertools.chain.from_iterable(meths))
