"""award not-presented flag, top awards boundary resolution

Revision ID: 7f25ecd68f1a
Revises: 41bd3d7b7998
Create Date: 2026-08-30 17:46:19.091403

"""
from typing import Sequence, Union

from alembic import op
import sqlalchemy as sa


# revision identifiers, used by Alembic.
revision: str = '7f25ecd68f1a'
down_revision: Union[str, Sequence[str], None] = '41bd3d7b7998'
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    """Upgrade schema."""
    # NOTE: hand-adjusted after autogenerate, same reason as prior
    # migrations that added a NOT NULL boolean — needs a server_default so
    # this applies cleanly against a database that already has award rows.
    op.add_column('awards', sa.Column('marked_not_presented', sa.Boolean(), nullable=False, server_default=sa.false()))
    op.add_column('shows', sa.Column('top_awards_resolved_car_ids', sa.JSON(), nullable=True))


def downgrade() -> None:
    """Downgrade schema."""
    op.drop_column('shows', 'top_awards_resolved_car_ids')
    op.drop_column('awards', 'marked_not_presented')
