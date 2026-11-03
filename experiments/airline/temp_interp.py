"""Notebook-ready interpreted query schema (Pydantic v2)."""

from typing import Annotated, Any, Literal

from pydantic import BaseModel, ConfigDict, Field, model_validator


class SchemaModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


EntityType = Literal["Passenger", "Reservation"]

PropertyName = Literal[
    "id",
    "name",
    "reservation_id",
    "status",
    "refundable",
    "hours_until_departure",
]

ALLOWED_PROPERTIES = {
    "Passenger": {"id", "name"},
    "Reservation": {
        "reservation_id",
        "status",
        "refundable",
        "hours_until_departure",
    },
}


class EntityReference(SchemaModel):
    alias: str = Field(description="Unique query-local name, e.g. reservation")
    type: EntityType
    identifier: str | None = Field(
        default=None,
        description="Known entity ID; null when unresolved. Never invent an ID.",
    )


class PropertyReference(SchemaModel):
    entity: str = Field(description="Alias of an entity in this query")
    property: PropertyName


class Constraint(SchemaModel):
    target: PropertyReference
    operator: Literal["eq", "neq", "gt", "lt", "gte", "lte"]
    value: Any = Field(
        description="Literal comparison value explicitly supplied by the user"
    )


class RetrieveOperation(SchemaModel):
    intent: Literal["retrieve"]
    action: Literal["lookup_reservation", "check_cancellation_eligibility"]
    target: str = Field(description="Alias of the target reservation")
    requested_properties: list[PropertyReference] = Field(default_factory=list)


class ExecuteOperation(SchemaModel):
    intent: Literal["execute"]
    action: Literal["cancel_reservation"]
    target: str = Field(description="Alias of the target reservation")


class Clarification(SchemaModel):
    field: str = Field(
        description="Missing or ambiguous field, e.g. entities.reservation.identifier"
    )
    question: str


class InterpretedQuery(SchemaModel):
    goal: str = Field(description="Short description of the user's desired outcome")

    operation: Annotated[
        RetrieveOperation | ExecuteOperation,
        Field(discriminator="intent"),
    ]

    entities: list[EntityReference] = Field(min_length=1)

    constraints: list[Constraint] = Field(
        default_factory=list,
        description=(
            "User-specified filters, combined with AND. "
            "Do not insert policy requirements or inferred database facts."
        ),
    )

    clarifications: list[Clarification] = Field(default_factory=list)

    @model_validator(mode="after")
    def validate_references(self):
        entities = {entity.alias: entity for entity in self.entities}

        if len(entities) != len(self.entities):
            raise ValueError("Entity aliases must be unique")

        target = entities.get(self.operation.target)
        if target is None or target.type != "Reservation":
            raise ValueError("Operation target must reference a Reservation")

        references = [constraint.target for constraint in self.constraints]
        if isinstance(self.operation, RetrieveOperation):
            references.extend(self.operation.requested_properties)

        for reference in references:
            entity = entities.get(reference.entity)
            if entity is None:
                raise ValueError(f"Unknown entity alias: {reference.entity}")

            if reference.property not in ALLOWED_PROPERTIES[entity.type]:
                raise ValueError(
                    f"{reference.property} is not a property of {entity.type}"
                )

        return self


# Example: execution intent does not establish eligibility or authorization.
if __name__ == "__main__":
    query = InterpretedQuery(
        goal="Cancel reservation ABC123",
        operation=ExecuteOperation(
            intent="execute",
            action="cancel_reservation",
            target="reservation",
        ),
        entities=[
            EntityReference(
                alias="reservation",
                type="Reservation",
                identifier="ABC123",
            )
        ],
    )
    print(query.model_dump_json(indent=2))
