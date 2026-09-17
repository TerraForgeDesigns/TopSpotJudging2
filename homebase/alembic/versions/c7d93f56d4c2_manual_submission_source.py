"""manual submission source and photo semantics

Revision ID: c7d93f56d4c2
Revises: 74239f009b3e
Create Date: 2026-09-06 12:00:00.000000

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


revision: str = "c7d93f56d4c2"
down_revision: Union[str, Sequence[str], None] = "74239f009b3e"
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.add_column(
        "judging_submissions",
        sa.Column("source", sa.Enum("HANDHELD", "HOMEBASE_MANUAL", native_enum=False, length=32), nullable=False, server_default="HANDHELD"),
    )
    op.add_column("judging_submissions", sa.Column("operator_label", sa.String(length=120), nullable=True))
    op.add_column(
        "judging_submissions",
        sa.Column("vehicle_photo_required", sa.Boolean(), nullable=False, server_default=sa.true()),
    )
    op.add_column(
        "judging_submissions",
        sa.Column("judge_sheet_photo_required", sa.Boolean(), nullable=False, server_default=sa.true()),
    )
    op.add_column("judging_submissions", sa.Column("vehicle_photo_path", sa.String(length=240), nullable=True))
    op.add_column("judging_submissions", sa.Column("judge_sheet_photo_path", sa.String(length=240), nullable=True))

    with op.batch_alter_table("judging_submissions") as batch_op:
        batch_op.alter_column("handheld_id", existing_type=sa.Integer(), nullable=True)


def downgrade() -> None:
    with op.batch_alter_table("judging_submissions") as batch_op:
        batch_op.alter_column("handheld_id", existing_type=sa.Integer(), nullable=False)

    op.drop_column("judging_submissions", "judge_sheet_photo_path")
    op.drop_column("judging_submissions", "vehicle_photo_path")
    op.drop_column("judging_submissions", "judge_sheet_photo_required")
    op.drop_column("judging_submissions", "vehicle_photo_required")
    op.drop_column("judging_submissions", "operator_label")
    op.drop_column("judging_submissions", "source")
