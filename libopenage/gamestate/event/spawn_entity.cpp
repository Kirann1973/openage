// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#include "spawn_entity.h"

#include "coord/phys.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/entity_factory.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"

namespace openage::gamestate::event {


Spawner::Spawner(const std::shared_ptr<openage::event::EventLoop> &loop) :
	EventEntity(loop) {
}

size_t Spawner::id() const {
	return 0;
}

std::string Spawner::idstr() const {
	return "spawner";
}


SpawnEntityHandler::SpawnEntityHandler(const std::shared_ptr<openage::event::EventLoop> &loop,
                                       const std::shared_ptr<gamestate::EntityFactory> &factory,
                                       const util::Path &animation_path) :
	OnceEventHandler("game.spawn_entity"),
	loop{loop},
	factory{factory},
	animation_path{animation_path} {
}

void SpawnEntityHandler::setup_event(const std::shared_ptr<openage::event::Event> & /* event */,
                                     const std::shared_ptr<openage::event::State> & /* state */) {
	// TODO
}

void SpawnEntityHandler::invoke(openage::event::EventLoop & /* loop */,
                                const std::shared_ptr<openage::event::EventEntity> & /* target */,
                                const std::shared_ptr<openage::event::State> &state,
                                const curve::time_t &time,
                                const param_map &params) {
	auto gstate = std::dynamic_pointer_cast<gamestate::GameState>(state);

	auto nyan_db = gstate->get_nyan_db();

	auto game_entities = nyan_db->get_obj_children_all("engine.util.game_entity.GameEntity");

	static uint8_t index = 0;
	static std::vector<nyan::fqon_t> test_entities = {
		"aoe1_base.data.game_entity.generic.chariot_archer.chariot_archer.ChariotArcher",
		"aoe1_base.data.game_entity.generic.bowman.bowman.Bowman",
		"aoe1_base.data.game_entity.generic.hoplite.hoplite.Hoplite",
		"aoe1_base.data.game_entity.generic.temple.temple.Temple",
		"aoe1_base.data.game_entity.generic.academy.academy.Academy",
	};
	nyan::fqon_t nyan_entity = test_entities.at(index);
	++index;
	if (index >= test_entities.size()) {
		index = 0;
	}

	// Create entity
	auto entity = this->factory->add_game_entity(this->loop, gstate, nyan_entity);

	// Setup components
	entity->push_to_render();

	gstate->add_game_entity(entity);
}

curve::time_t SpawnEntityHandler::predict_invoke_time(const std::shared_ptr<openage::event::EventEntity> & /* target */,
                                                      const std::shared_ptr<openage::event::State> & /* state */,
                                                      const curve::time_t &at) {
	return at;
}

} // namespace openage::gamestate::event
