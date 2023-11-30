from pydantic import BaseModel, Field


class EvaluationResult(BaseModel):
    """
    Structure containing results associated with a particular generated class.
    """

    true_positives: int
    false_positives: int
    false_negatives: int

    def get_precision(self) -> float:
        """
        Calculate and return precision.
        """
        if self.true_positives + self.false_positives == 0:
            return 0.0
        return float(self.true_positives) / float(
            self.true_positives + self.false_positives
        )

    def get_recall(self) -> float:
        """
        Calculate and return recall.
        """
        if self.true_positives + self.false_negatives == 0:
            return 0.0
        return float(self.true_positives) / float(
            self.true_positives + self.false_negatives
        )

    def get_fscore(self):
        """
        Calculate and return the F-1 score (combination of precision and recall).
        """
        p = self.get_precision()
        r = self.get_recall()
        if p + r == 0.0:
            return 0.0
        return (2.0 * p * r) / (p + r)


class EvaluationResults(BaseModel):
    result_mapping: dict[str, EvaluationResult] = Field(default_factory=dict)
