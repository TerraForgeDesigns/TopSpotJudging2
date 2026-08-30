"""wizard fields: show notes, show location optional, award active, show drafts

Revision ID: 674d2e0674cd
Revises: 8ad4e96210aa
Create Date: 2026-08-30 10:59:54.733184

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


# revision identifiers, used by Alembic.
revision: str = '674d2e0674cd'
down_revision: Union[str, Sequence[str], None] = '8ad4e96210aa'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    """Upgrade schema."""
    # NOTE: hand-adjusted after autogenerate — see DECISIONS.md. SQLite has
    # no native ALTER COLUMN / DROP NOT NULL, so the location-nullable
    # change needs batch mode (table-rebuild-under-the-hood). The new
    # awards.active column gets an explicit server_default so this applies
    # cleanly even against a database that already has award rows (not
    # true of this dev environment today, but the migration should still
    # be correct on its own terms).
    op.create_table(
        'show_drafts',
        sa.Column('id', sa.Integer(), nullable=False),
        sa.Column('created_at', sa.DateTime(timezone=True), nullable=False),
        sa.Column('data', sa.JSON(), nullable=False),
        sa.PrimaryKeyConstraint('id'),
    )
    op.add_column('awards', sa.Column('active', sa.Boolean(), nullable=False, server_default=sa.true()))
    op.add_column('shows', sa.Column('notes', sa.String(length=2000), nullable=True))
    with op.batch_alter_table('shows') as batch_op:
        batch_op.alter_column('location', existing_type=sa.VARCHAR(length=200), nullable=True)


def downgrade() -> None:
    """Downgrade schema."""
    with op.batch_alter_table('shows') as batch_op:
        batch_op.alter_column('location', existing_type=sa.VARCHAR(length=200), nullable=False)
    op.drop_column('shows', 'notes')
    op.drop_column('awards', 'active')
    op.drop_table('show_drafts')
