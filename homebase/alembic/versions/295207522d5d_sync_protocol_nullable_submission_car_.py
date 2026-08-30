"""sync protocol: nullable submission car_id, registration_number, closed_at, unique handheld label

Revision ID: 295207522d5d
Revises: d96c4ade65f2
Create Date: 2026-08-29 20:30:32.542616

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


# revision identifiers, used by Alembic.
revision: str = '295207522d5d'
down_revision: Union[str, Sequence[str], None] = 'd96c4ade65f2'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    """Upgrade schema. SQLite has no ALTER for constraints/column type changes,
    so this uses batch mode (copy-and-move) for both tables touched."""
    with op.batch_alter_table("handhelds", schema=None) as batch_op:
        batch_op.create_unique_constraint("uq_handhelds_label", ["label"])

    with op.batch_alter_table("judging_submissions", schema=None) as batch_op:
        batch_op.add_column(sa.Column("registration_number", sa.String(length=40), nullable=False, server_default=""))
        batch_op.add_column(sa.Column("closed_at", sa.DateTime(timezone=True), nullable=False, server_default=sa.text("CURRENT_TIMESTAMP")))
        batch_op.alter_column("car_id", existing_type=sa.INTEGER(), nullable=True)
        batch_op.create_index(op.f("ix_judging_submissions_registration_number"), ["registration_number"], unique=False)

    # server_default was only needed to backfill any existing rows (dev DBs
    # may already have judging_submissions data); the app never relies on a
    # DB-side default for these columns going forward.
    with op.batch_alter_table("judging_submissions", schema=None) as batch_op:
        batch_op.alter_column("registration_number", server_default=None)
        batch_op.alter_column("closed_at", server_default=None)


def downgrade() -> None:
    """Downgrade schema."""
    with op.batch_alter_table("judging_submissions", schema=None) as batch_op:
        batch_op.drop_index(op.f("ix_judging_submissions_registration_number"))
        batch_op.alter_column("car_id", existing_type=sa.INTEGER(), nullable=False)
        batch_op.drop_column("closed_at")
        batch_op.drop_column("registration_number")

    with op.batch_alter_table("handhelds", schema=None) as batch_op:
        batch_op.drop_constraint("uq_handhelds_label", type_="unique")
