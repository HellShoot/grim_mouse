/* ResidualVM - A 3D game interpreter
 *
 * ResidualVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "common/endian.h"

#include "engines/grim/debug.h"
#include "engines/grim/costume.h"
#include "engines/grim/grim.h"
#include "engines/grim/resource.h"
#include "engines/grim/emi/costumeemi.h"
#include "engines/grim/emi/modelemi.h"
#include "engines/grim/costume/head.h"
#include "engines/grim/emi/costume/emianim_component.h"
#include "engines/grim/emi/costume/emiluavar_component.h"
#include "engines/grim/emi/costume/emiluacode_component.h"
#include "engines/grim/emi/costume/emimesh_component.h"
#include "engines/grim/emi/costume/emiskel_component.h"
#include "engines/grim/emi/costume/emisprite_component.h"
#include "engines/grim/emi/costume/emitexi_component.h"

namespace Grim {

EMICostume::EMICostume(const Common::String &fname, Costume *prevCost) :
		Costume(fname, prevCost), _wearChore(NULL), _emiSkel(NULL) {
}

void EMICostume::load(Common::SeekableReadStream *data) {
	Common::Array<Component *> components;

	_numChores = data->readUint32LE();
	_chores = new Chore *[_numChores];
	for (int i = 0; i < _numChores; i++) {
		uint32 nameLength;
		Component *prevComponent = NULL;
		nameLength = data->readUint32LE();
		assert(nameLength < 32);

		char name[32];
		data->read(name, nameLength);
		char f[4];
		data->read(f, 4);
		float length = get_float(f);
		int numTracks = data->readUint32LE();

		if (length < 1000)
			length *= 1000;

		EMIChore *chore = new EMIChore(name, i, this, (int)length, numTracks);
		_chores[i] = chore;

		for (int k = 0; k < numTracks; k++) {
			int componentNameLength = data->readUint32LE();

			char *componentName = new char[componentNameLength];
			data->read(componentName, componentNameLength);

			data->readUint32LE();
			int parentID = data->readUint32LE();
			if (parentID == -1 && _prevCostume) {
				// However, only the first item can actually share the
				// node hierarchy with the previous costume, so flag
				// that component so it knows what to do
				if (i == 0)
					parentID = -2;
				prevComponent = _prevCostume->getComponent(0);
				// Make sure that the component is valid
				if (!prevComponent->isComponentType('M', 'M', 'D', 'L'))
					prevComponent = NULL;
			}
			// Actually load the appropriate component
			Component *component = loadEMIComponent(parentID < 0 ? NULL : components[parentID], parentID, componentName, prevComponent);
			if (component) {
				component->setCostume(this);
				component->init();
				chore->addComponent(component);
			}

			components.push_back(component);

			ChoreTrack &track = chore->_tracks[k];
			track.numKeys = data->readUint32LE();
			track.keys = new TrackKey[track.numKeys];
			track.component = component;
			track.compID = -1; // -1 means "look at .component"

			for (int j = 0; j < track.numKeys; j++) {
				float time, value;
				char v[8];
				data->read(v, 8);
				time = get_float(v);
				value = get_float(v + 4);
				track.keys[j].time = (int)(time * 1000);
				length = MAX(length, time * 1000);
				track.keys[j].value = (int)value;
			}
			delete[] componentName;
		}

		// Some chores report duration 1000 while they have components with
		// keyframes after 1000. See elaine_wedding/take_contract, for example.
		chore->_length = ceil(length);
	}

	_numComponents = components.size();
	_components = new Component *[_numComponents];
	for (int i = 0; i < _numComponents; ++i) {
		_components[i] = components[i];
	}
}

void EMICostume::playChore(int num) {
	EMIChore *chore = static_cast<EMIChore *>(_chores[num]);
	if (chore->isWearChore()) {
		setWearChore(chore);
	}
	Costume::playChore(num);
}

void EMICostume::playChoreLooping(int num) {
	EMIChore *chore = static_cast<EMIChore *>(_chores[num]);
	if (chore->isWearChore()) {
		setWearChore(chore);
	}
	Costume::playChoreLooping(num);
}

Component *EMICostume::loadEMIComponent(Component *parent, int parentID, const char *name, Component *prevComponent) {
	assert(name[0] == '!');
	++name;

	char type[5];
	tag32 tag = 0;
	memcpy(&tag, name, 4);
	memcpy(&type, name, 4);
	type[4] = 0;
	tag = FROM_BE_32(tag);

	name += 4;

	if (tag == MKTAG('m', 'e', 's', 'h')) {
		return new EMIMeshComponent(parent, parentID, name, prevComponent, tag, this);
	} else if (tag == MKTAG('s', 'k', 'e', 'l')) {
		return new EMISkelComponent(parent, parentID, name, prevComponent, tag);
	} else if (tag == MKTAG('t', 'e', 'x', 'i')) {
		return new EMITexiComponent(parent, parentID, name, prevComponent, tag);
	} else if (tag == MKTAG('a', 'n', 'i', 'm')) {
		return new EMIAnimComponent(parent, parentID, name, prevComponent, tag);
	} else if (tag == MKTAG('l', 'u', 'a', 'c')) {
		return new EMILuaCodeComponent(parent, parentID, name, prevComponent, tag);
	} else if (tag == MKTAG('l', 'u', 'a', 'v')) {
		return new EMILuaVarComponent(parent, parentID, name, prevComponent, tag);
	} else if (tag == MKTAG('s', 'p', 'r', 't')) {
		return new EMISpriteComponent(parent, parentID, name, prevComponent, tag);
	} else if (tag == MKTAG('s', 'h', 'a', 'd')) {
		Debug::warning(Debug::Costumes, "Actor::loadComponentEMI Implement SHAD-handling: %s" , name);
	} else if (tag == MKTAG('a', 'w', 'g', 't')) {
		Debug::warning(Debug::Costumes, "Actor::loadComponentEMI Implement AWGT-handling: %s" , name);
	} else if (tag == MKTAG('s', 'n', 'd', '2')) {
		// ignore, this is a leftover from an earlier engine.
	} else {
		error("Actor::loadComponentEMI missing tag: %s for %s", name, type);
	}

	return NULL;
}

void EMICostume::draw() {
	bool drewMesh = false;
	for (Common::List<Chore*>::iterator it = _playingChores.begin(); it != _playingChores.end(); ++it) {
		Chore *c = (*it);
		for (int i = 0; i < c->_numTracks; ++i) {
			if (c->_tracks[i].component) {
				c->_tracks[i].component->draw();
				if (c->_tracks[i].component->isComponentType('m', 'e', 's', 'h'))
					drewMesh = true;
			}
		}
	}

	if (_wearChore && !drewMesh) {
		_wearChore->getMesh()->draw();
	}
}

int EMICostume::update(uint time) {
	if (_emiSkel)
		_emiSkel->reset();
	for (Common::List<Chore*>::iterator i = _playingChores.begin(); i != _playingChores.end(); ++i) {
		Chore *c = *i;
		c->update(time);

		for (int t = 0; t < c->_numTracks; ++t) {
			if (c->_tracks[t].component) {
				c->_tracks[t].component->update(time);
			}
		}

		if (!c->isPlaying()) {
			i = _playingChores.erase(i);
			--i;
		}
	}
	if (_emiSkel)
		_emiSkel->commit();

	return 0;
}

void EMICostume::saveState(SaveGame *state) const {
	Costume::saveState(state);
	Common::List<Material *>::const_iterator it = _materials.begin();
	for (; it != _materials.end(); ++it) {
		state->writeLESint32((*it)->getActiveTexture());
	}
	state->writeLESint32(_wearChore ? _wearChore->getChoreId() : -1);
}

bool EMICostume::restoreState(SaveGame *state) {
	bool ret = Costume::restoreState(state);
	if (ret) {
		Common::List<Material *>::const_iterator it = _materials.begin();
		for (; it != _materials.end(); ++it) {
			(*it)->setActiveTexture(state->readLESint32());
		}

		int id = state->readLESint32();
		if (id >= 0) {
			EMIChore *chore = static_cast<EMIChore *>(_chores[id]);
			setWearChore(chore);
		}
	}
	return ret;
}

Material *EMICostume::findMaterial(const Common::String &name) {
	Common::String fixedName = g_resourceloader->fixFilename(name, false);
	Common::List<Material *>::iterator it = _materials.begin();
	for (; it != _materials.end(); ++it) {
		if ((*it)->getFilename() == fixedName) {
			return *it;
		}
	}
	return NULL;
}

Material *EMICostume::loadMaterial(const Common::String &name) {
	Material *mat = findMaterial(name);
	if (!mat) {
		mat = g_resourceloader->loadMaterial(name.c_str(), NULL);
		_materials.push_back(mat);
	}
	return mat;
}

void EMICostume::setWearChore(EMIChore *chore) {
	if (chore != _wearChore) {
		_wearChore = chore;

		if (_emiSkel) {
			_emiSkel->reset();
		}
		_emiSkel = chore->getSkeleton();
	}
}

} // end of namespace Grim
