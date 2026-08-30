"""add show status

Revision ID: 90ab4443243d
Revises: 674d2e0674cd
Create Date: 2026-08-30 15:38:29.993633

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


# revision identifiers, used by Alembic.
revision: str = '90ab4443243d'
down_revision: Union[str, Sequence[str], None] = '674d2e0674cd'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    """Upgrade schema."""
    # NOTE: hand-adjusted after autogenerate, same reason as 674d2e0674cd's
    # awards.active column — a NOT NULL column needs a server_default so
    # this applies cleanly against a database that already has show rows.
    op.add_column(
        'shows',
        sa.Column(
            'status',
            sa.Enum('SETUP', 'JUDGING', 'FINISHED', name='showstatus', native_enum=False, length=16),
            nullable=False,
            server_default='setup',
        ),
    )


def downgrade() -> None:
    """Downgrade schema."""
    op.drop_column('shows', 'status')
