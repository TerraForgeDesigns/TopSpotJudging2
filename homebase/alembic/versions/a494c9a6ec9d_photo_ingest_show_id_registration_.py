"""photo ingest: show_id, registration_number, status, thumbnail, nullable car_id

Revision ID: a494c9a6ec9d
Revises: 9e0fad58414a
Create Date: 2026-08-30 09:00:00.000000

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


# revision identifiers, used by Alembic.
revision: str = 'a494c9a6ec9d'
down_revision: Union[str, Sequence[str], None] = '9e0fad58414a'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    """Upgrade schema. Batch mode: SQLite can't ADD COLUMN NOT NULL, ALTER
    for a new foreign key, or relax an existing NOT NULL without a rebuild."""
    with op.batch_alter_table("photos", schema=None) as batch_op:
        batch_op.add_column(sa.Column("show_id", sa.Integer(), nullable=False, server_default="0"))
        batch_op.add_column(sa.Column("registration_number", sa.String(length=40), nullable=False, server_default=""))
        batch_op.add_column(sa.Column("status", sa.String(length=16), nullable=False, server_default="matched"))
        batch_op.add_column(sa.Column("thumbnail_path", sa.String(length=500), nullable=True))
        batch_op.add_column(sa.Column("original_filename", sa.String(length=255), nullable=False, server_default=""))
        batch_op.alter_column("car_id", existing_type=sa.INTEGER(), nullable=True)
        batch_op.create_index(op.f("ix_photos_registration_number"), ["registration_number"], unique=False)
        batch_op.create_index(op.f("ix_photos_show_id"), ["show_id"], unique=False)
        batch_op.create_foreign_key("fk_photos_show_id_shows", "shows", ["show_id"], ["id"])

    with op.batch_alter_table("photos", schema=None) as batch_op:
        batch_op.alter_column("show_id", server_default=None)
        batch_op.alter_column("registration_number", server_default=None)
        batch_op.alter_column("status", server_default=None)
        batch_op.alter_column("original_filename", server_default=None)


def downgrade() -> None:
    """Downgrade schema."""
    with op.batch_alter_table("photos", schema=None) as batch_op:
        batch_op.drop_constraint("fk_photos_show_id_shows", type_="foreignkey")
        batch_op.drop_index(op.f("ix_photos_show_id"))
        batch_op.drop_index(op.f("ix_photos_registration_number"))
        batch_op.alter_column("car_id", existing_type=sa.INTEGER(), nullable=False)
        batch_op.drop_column("original_filename")
        batch_op.drop_column("thumbnail_path")
        batch_op.drop_column("status")
        batch_op.drop_column("registration_number")
        batch_op.drop_column("show_id")
