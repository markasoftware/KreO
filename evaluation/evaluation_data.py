from pydantic import BaseModel, Field


class EvaluationResult(BaseModel):
    """
    Structure containing results associated with a particular generated class.
    """

    true_positives: int
    false_positives: int
    false_negatives: int


class EvaluationResults(BaseModel):
    result_mapping: dict[str, EvaluationResult] = Field(default_factory=dict)
