"""add show_id to judging_submissions

Revision ID: 9e0fad58414a
Revises: 295207522d5d
Create Date: 2026-08-29 20:36:15.170149

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


# revision identifiers, used by Alembic.
revision: str = '9e0fad58414a'
down_revision: Union[str, Sequence[str], None] = '295207522d5d'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    """Upgrade schema. Batch mode: SQLite can't ADD COLUMN NOT NULL or
    ALTER for a new foreign key without a table rebuild."""
    with op.batch_alter_table("judging_submissions", schema=None) as batch_op:
        batch_op.add_column(sa.Column("show_id", sa.Integer(), nullable=False, server_default="0"))
        batch_op.create_index(op.f("ix_judging_submissions_show_id"), ["show_id"], unique=False)
        batch_op.create_foreign_key("fk_judging_submissions_show_id_shows", "shows", ["show_id"], ["id"])

    with op.batch_alter_table("judging_submissions", schema=None) as batch_op:
        batch_op.alter_column("show_id", server_default=None)


def downgrade() -> None:
    """Downgrade schema."""
    with op.batch_alter_table("judging_submissions", schema=None) as batch_op:
        batch_op.drop_constraint("fk_judging_submissions_show_id_shows", type_="foreignkey")
        batch_op.drop_index(op.f("ix_judging_submissions_show_id"))
        batch_op.drop_column("show_id")
