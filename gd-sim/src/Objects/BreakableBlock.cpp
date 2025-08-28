#include <Block.hpp>
#include <Player.hpp>

bool BreakableBlock::touching(Player const& p) const {
	if (Block::touching(p)) {
		return !p.usedEffects.contains(id);
	}

	return false;
}

void BreakableBlock::collide(Player& p) const {

	// Breakable block hitboxes have a slightly larger break hitbox than a regular block
	bool broken = false;
	if (p.gravTop(*this) > p.gravBottom(p) + p.grav(5) && p.grav(pos.y) < p.grav(p.pos.y)) {
		broken = true;
	} else if (p.gravBottom(*this) < p.gravTop(p) - p.grav(5) && p.grav(pos.y) > p.grav(p.pos.y)) {
		broken = true;
	} else {
		// To handle snapping accordingly, hand it off to Block
		Block::collide(p);
		broken = p.dead;

		// Reset fields that block sets
		p.dead = false;
		if (p.grounded)
			p.snapData.playerFrame = 0;
	}

	if (broken) {
		// Can't break a block twice
		p.usedEffects.insert(id);
	}
}
