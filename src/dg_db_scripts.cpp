/* ************************************************************************
*  File: dg_db_scripts.cpp                                 Part of Bylins *
*                                                                         *
*  Usage: Contains routines to handle db functions for scripts and trigs  *
*                                                                         *
*  All rights reserved.  See license.doc for complete information.        *
*                                                                         *
*  Death's Gate MUD is based on CircleMUD, Copyright (C) 1993, 94.        *
*  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
*                                                                         *
*  $Author$                                                         *
*  $Date$                                           *
*  $Revision$                                                   *
************************************************************************ */

#include "obj.hpp"
#include "dg_scripts.h"
#include "db.h"
#include "handler.h"
#include "dg_event.h"
#include "comm.h"
#include "spells.h"
#include "skills.h"
#include "im.h"
#include "features.hpp"
#include "char.hpp"
#include "interpreter.h"
#include "room.hpp"
#include "magic.h"
#include "boards.h"
#include "utils.h"
#include "structs.h"
#include "sysdep.h"
#include "conf.h"

#include <algorithm>
#include <stack>

void trig_data_copy(TRIG_DATA * this_data, const TRIG_DATA * trg);
void trig_data_free(TRIG_DATA * this_data);

extern INDEX_DATA **trig_index;
extern int top_of_trigt;

extern INDEX_DATA *mob_index;

int check_recipe_values(CHAR_DATA * ch, int spellnum, int spelltype, int showrecipe);

char* indent_trigger(char* cmd , int* level)
{
	static std::stack<std::string> indent_stack;

	*level = std::max(0, *level);
	if (*level == 0)
	{
		std::stack<std::string> empty_stack;
		indent_stack.swap(empty_stack);
	}

	// ������ ��������
	int currlev, nextlev;
	currlev = nextlev = *level;

	if (!cmd) return cmd;

	// ������� ������� ������ �������.
	char* ptr = cmd;
	skip_spaces(&ptr);

	// ptr �������� ������ ��� ������ ��������.
	if (!strn_cmp("case ", ptr , 5) || !strn_cmp("default", ptr , 7))
	{
		// ���������������� case (��� default ����� case) ��� break
		if (!indent_stack.empty()
			&& !strn_cmp("case ", indent_stack.top().c_str(), 5))
		{
			--currlev;
		}
		else
		{
			indent_stack.push(ptr);
		}
		nextlev = currlev + 1;
	}
	else if (!strn_cmp("if ", ptr , 3) || !strn_cmp("while ", ptr , 6)
		|| !strn_cmp("foreach ", ptr, 8) || !strn_cmp("switch ", ptr, 7))
	{
		++nextlev;
		indent_stack.push(ptr);
	}
	else if (!strn_cmp("elseif ", ptr, 7) || !strn_cmp("else", ptr, 4))
	{
		--currlev;
	}
	else if (!strn_cmp("break", ptr, 5) || !strn_cmp("end", ptr, 3)
		|| !strn_cmp("done", ptr, 4))
	{
		// � switch ����������� break ����� �������� � ����� ������ done|end
		if ((!strn_cmp("done", ptr, 4) || !strn_cmp("end", ptr, 3))
			&& !indent_stack.empty()
			&& (!strn_cmp("case ", indent_stack.top().c_str(), 5)
				|| !strn_cmp("default", indent_stack.top().c_str() , 7)))
		{
			--currlev;
			--nextlev;
			indent_stack.pop();
		}
		if (!indent_stack.empty())
		{
			indent_stack.pop();
		}
		--nextlev;
		--currlev;
	}

	if (nextlev < 0) nextlev = 0;
	if (currlev < 0) currlev = 0;

	// ��������� �������������� �������

	char* tmp = (char *) malloc(currlev * 2 + 1);
	memset(tmp, 0x20, currlev*2);
	tmp[currlev*2] = '\0';

	tmp = str_add(tmp, ptr);

	cmd = (char *)realloc(cmd, strlen(tmp) + 1);
	cmd = strcpy(cmd, tmp);

	free(tmp);

	*level = nextlev;
	return cmd;
}

void parse_trigger(FILE * trig_f, int nr)
{
	int t[2], k, attach_type, indlev;

	char line[256], *cmds, flags[256], *s;
	struct cmdlist_element *cle;
	index_data *index;
	TRIG_DATA *trig;
	trig = new TRIG_DATA();
	//CREATE(trig, 1);
	CREATE(index, 1);

	index->vnum = nr;
	index->number = 0;
	index->func = NULL;
	index->proto = trig;

	sprintf(buf2, "trig vnum %d", nr);

	trig->nr = top_of_trigt;
	trig->name = fread_string(trig_f, buf2);

	get_line(trig_f, line);
	k = sscanf(line, "%d %s %d", &attach_type, flags, t);
	trig->attach_type = (byte) attach_type;
	asciiflag_conv(flags, &trig->trigger_type);
	trig->narg = (k == 3) ? t[0] : 0;

	trig->arglist = fread_string(trig_f, buf2);

	s = cmds = fread_string(trig_f, buf2);

	CREATE(trig->cmdlist, 1);

	trig->cmdlist->cmd = str_dup(strtok(s, "\n\r"));

	indlev = 0;
	trig->cmdlist->cmd = indent_trigger(trig->cmdlist->cmd, &indlev);

	cle = trig->cmdlist;

	while ((s = strtok(NULL, "\n\r")))
	{
		CREATE(cle->next, 1);
		cle = cle->next;
		cle->cmd = str_dup(s);
		cle->cmd = indent_trigger(cle->cmd, &indlev);
	}
	if (indlev > 0)
	{
		char tmp[MAX_INPUT_LENGTH];
		snprintf(tmp, sizeof(tmp),
			"Positive indent-level on trigger #%d end.", nr);
		log("%s",tmp);
		Boards::dg_script_text += tmp + std::string("\r\n");
	}

	free(cmds);
	
	trig_index[top_of_trigt++] = index;
}


/*
 * create a new trigger from a prototype.
 * nr is the real number of the trigger.
 */
TRIG_DATA *read_trigger(int nr)
{
	index_data *index;
	TRIG_DATA *trig = new TRIG_DATA();
	if (nr >= top_of_trigt || nr == -1)
		return NULL;
	if ((index = trig_index[nr]) == NULL)
		return NULL;


	trig_data_copy(trig, index->proto);

	index->number++;

	return trig;
}


// release memory allocated for a variable list
void free_varlist(struct trig_var_data *vd)
{
	struct trig_var_data *i, *j;

	for (i = vd; i;)
	{
		j = i;
		i = i->next;
		if (j->name)
			free(j->name);
		if (j->value)
			free(j->value);
		free(j);
	}
}

// release memory allocated for a script
void free_script(SCRIPT_DATA * sc)
{
	if (sc == NULL)
		return;

	extract_script(sc);

	free_varlist(sc->global_vars);

	free(sc);
}

void trig_data_init(TRIG_DATA * this_data)
{
	this_data->nr = NOTHING;
	this_data->data_type = 0;
	this_data->name = NULL;
	this_data->trigger_type = 0;
	this_data->cmdlist = NULL;
	this_data->curr_state = NULL;
	this_data->narg = 0;
	this_data->arglist = NULL;
	this_data->depth = 0;
	this_data->wait_event = NULL;
	this_data->purged = FALSE;
	this_data->var_list = NULL;

	this_data->next = NULL;
}


void trig_data_copy(TRIG_DATA * this_data, const TRIG_DATA * trg)
{
	trig_data_init(this_data);
	this_data->nr = trg->nr;
	this_data->attach_type = trg->attach_type;
	this_data->data_type = trg->data_type;
	this_data->name = str_dup(trg->name);
	this_data->trigger_type = trg->trigger_type;
	this_data->cmdlist = trg->cmdlist;
	this_data->narg = trg->narg;
	if (trg->arglist)
	{
		this_data->arglist = str_dup(trg->arglist);
	}
}


void trig_data_free(TRIG_DATA * this_data)
{
	//    struct cmdlist_element *i, *j;

	free(this_data->name);

	/*
	 * The command list is a memory leak right now!
	 *
	 if (cmdlist != trigg->cmdlist || this_data->proto)
	 for (i = cmdlist; i;)
	 {j = i;
	 i = i->next;
	 free(j->cmd);
	 free(j);
	 }
	 */

	if (this_data->arglist)	// �� ����� ������� free( NULL )
		free(this_data->arglist);

	free_varlist(this_data->var_list);

	if (this_data->wait_event)
	{
		// �� ����� �������� ����������� callback ����� trig_wait_event
		// � ������ ��������� ������������ ��������� info
		free(this_data->wait_event->info);
		remove_event(this_data->wait_event);
	}

	free(this_data);
}

// for mobs and rooms:
void dg_read_trigger(FILE * fp, void *proto, int type)
{
	char line[256];
	char junk[8];
	int vnum, rnum, count;
	CHAR_DATA *mob;
	ROOM_DATA *room;

	get_line(fp, line);
	count = sscanf(line, "%s %d", junk, &vnum);

	if (count != 2)  	// should do a better job of making this message
	{
		log("SYSERR: Error assigning trigger!");
		return;
	}
	
	rnum = real_trigger(vnum);
	if (rnum < 0)
	{
		sprintf(line, "SYSERR: Trigger vnum #%d asked for but non-existant!", vnum);
		log("%s",line);
		return;
	}

	switch (type)
	{
	case MOB_TRIGGER:
		mob = (CHAR_DATA *) proto;
		mob->proto_script.push_back(vnum);
		
		break;

	case WLD_TRIGGER:
		room = (ROOM_DATA *) proto;
		room->proto_script.push_back(vnum);

		if (rnum >= 0)
		{
			if (!(room->script))
				CREATE(room->script, 1);
			add_trigger(SCRIPT(room), read_trigger(rnum), -1);
			if (trig_index[rnum]->proto->owner.size() > 0 && trig_index[rnum]->proto->owner.find(-1) != trig_index[rnum]->proto->owner.end())
			{
				trig_index[rnum]->proto->owner[-1].push_back(room->number);
			}
			else
			{
				std::vector<int> tmp_vector;
				tmp_vector.push_back(room->number);
				trig_index[rnum]->proto->owner.insert(std::pair<int, std::vector<int>>(-1, tmp_vector));
			}
		}
		else
		{
			sprintf(line, "SYSERR: non-existant trigger #%d assigned to room #%d", vnum, room->number);
			log("%s",line);
		}
		break;

	default:
		sprintf(line, "SYSERR: Trigger vnum #%d assigned to non-mob/obj/room", vnum);
		log("%s",line);
	}
}

void dg_obj_trigger(char *line, OBJ_DATA * obj)
{
	char junk[8];
	int vnum, rnum, count;

	count = sscanf(line, "%s %d", junk, &vnum);

	if (count != 2)  	// should do a better job of making this message
	{
		log("SYSERR: Error assigning trigger!");
		return;
	}

	rnum = real_trigger(vnum);
	if (rnum < 0)
	{
		sprintf(line, "SYSERR: Trigger vnum #%d asked for but non-existant!", vnum);
		log("%s",line);
		return;
	}
	if (trig_index[rnum]->proto->owner.find(-1) != trig_index[rnum]->proto->owner.end())
	{
		trig_index[rnum]->proto->owner[-1].push_back(vnum);
	}
	else
	{
		std::vector<int> tmp_vector;
		tmp_vector.push_back(vnum);
		trig_index[rnum]->proto->owner.insert(std::pair<int, std::vector<int>>(-1, tmp_vector));
	}
	obj->proto_script.push_back(vnum);
}

extern CHAR_DATA *mob_proto;

void assign_triggers(void *i, int type)
{
	CHAR_DATA *mob;
	OBJ_DATA *obj;
	ROOM_DATA *room;
	int rnum;
	char buf[256];

	switch (type)
	{
	case MOB_TRIGGER:
		mob = (CHAR_DATA *) i;
		for (const auto trigger_vnum : mob_proto[GET_MOB_RNUM(mob)].proto_script)
		{
			rnum = real_trigger(trigger_vnum);
			if (rnum == -1)
			{
				sprintf(buf, "SYSERR: trigger #%d non-existant, for mob #%d",
					trigger_vnum, mob_index[mob->nr].vnum);
				log("%s",buf);
			}
			else
			{
				if (trig_index[rnum]->proto->attach_type != MOB_TRIGGER)
				{
					sprintf(buf, "SYSERR: trigger #%d has wrong attach_type: %d, for mob #%d",
						trigger_vnum, static_cast<int>(trig_index[rnum]->proto->attach_type),
						mob_index[mob->nr].vnum);
					mudlog(buf, BRF, LVL_BUILDER, ERRLOG, TRUE);
				}
				else
				{
					if (!SCRIPT(mob))
					{
						CREATE(SCRIPT(mob), 1);
					}
					add_trigger(SCRIPT(mob), read_trigger(rnum), -1);
					if (trig_index[rnum]->proto->owner.find(-1) != trig_index[rnum]->proto->owner.end())
					{
						trig_index[rnum]->proto->owner[-1].push_back(GET_MOB_VNUM(mob));
					}
					else 
					{
						std::vector<int> tmp_vector;
						tmp_vector.push_back(GET_MOB_VNUM(mob));
						trig_index[rnum]->proto->owner.insert(std::pair<int, std::vector<int>>(-1, tmp_vector));
					}
						

				}
			}
		}
		break;

	case OBJ_TRIGGER:
		obj = (OBJ_DATA *) i;
		for (const auto trigger_vnum : obj_proto.proto_script(GET_OBJ_RNUM(obj)))
		{
			rnum = real_trigger(trigger_vnum);
			if (rnum == -1)
			{
				sprintf(buf, "SYSERR: trigger #%d non-existant, for obj #%d",
					trigger_vnum, obj_proto.vnum(obj));
				log("%s",buf);
			}
			else
			{
				if (trig_index[rnum]->proto->attach_type != OBJ_TRIGGER)
				{
					sprintf(buf, "SYSERR: trigger #%d has wrong attach_type: %d, for obj #%d",
						trigger_vnum,
						static_cast<int>(trig_index[rnum]->proto->attach_type),
						obj_proto.vnum(obj->item_number));
					mudlog(buf, BRF, LVL_BUILDER, ERRLOG, TRUE);
				}
				else
				{
					if (!SCRIPT(obj))
					{
						CREATE(SCRIPT(obj), 1);
					}
					
					add_trigger(SCRIPT(obj), read_trigger(rnum), -1);
				}
			}
		}
		break;

	case WLD_TRIGGER:
		room = (ROOM_DATA *) i;
		for (const auto trigger_vnum : room->proto_script)
		{
			rnum = real_trigger(trigger_vnum);
			if (rnum == -1)
			{
				sprintf(buf, "SYSERR: trigger #%d non-existant, for room #%d",
					trigger_vnum, room->number);
				log("%s",buf);
			}
			else
			{
				if (trig_index[rnum]->proto->attach_type != WLD_TRIGGER)
				{
					sprintf(buf, "SYSERR: trigger #%d has wrong attach_type: %d, for room #%d",
						trigger_vnum, static_cast<int>(trig_index[rnum]->proto->attach_type),
						room->number);
					mudlog(buf, BRF, LVL_BUILDER, ERRLOG, TRUE);
				}
				else
				{
					if (!SCRIPT(room))
					{
						CREATE(SCRIPT(room), 1);
					}
					add_trigger(SCRIPT(room), read_trigger(rnum), -1);
					if (trig_index[rnum]->proto->owner.find(-1) != trig_index[rnum]->proto->owner.end())
					{
						trig_index[rnum]->proto->owner[-1].push_back(room->number);
						
					}
					else
					{
						std::vector<int> tmp_vector;
						tmp_vector.push_back(room->number);
						trig_index[rnum]->proto->owner.insert(std::pair<int, std::vector<int>>(-1, tmp_vector));
					}
				}
			}
		}
		break;

	default:
		log("SYSERR: unknown type for assign_triggers()");
		break;
	}
}

void trg_featturn(CHAR_DATA * ch, int featnum, int featdiff)
{
	if (HAVE_FEAT(ch, featnum))
	{
		if (featdiff)
			return;
		else
		{
			sprintf(buf, "�� �������� ����������� '%s'.\r\n", feat_info[featnum].name);
			send_to_char(buf, ch);
			UNSET_FEAT(ch, featnum);
		}
	}
	else
	{
		if (featdiff)
		{
			sprintf(buf, "�� ������ ����������� '%s'.\r\n", feat_info[featnum].name);
			send_to_char(buf, ch);
			if (feat_info[featnum].classknow[(int) GET_CLASS(ch)][(int) GET_KIN(ch)])
				SET_FEAT(ch, featnum);
		};
	}
}

void trg_skillturn(CHAR_DATA * ch, const ESkill skillnum, int skilldiff, int vnum)
{
	const int ch_kin = static_cast<int>(GET_KIN(ch));
	const int ch_class = static_cast<int>(GET_CLASS(ch));

	if (ch->get_trained_skill(skillnum))
	{
		if (skilldiff)
		{
			return;
		}

		ch->set_skill(skillnum, 0);
		send_to_char(ch, "��� ������ ������ '%s'.\r\n", skill_name(skillnum));
		log("Remove %s from %s (trigskillturn)", skill_name(skillnum), GET_NAME(ch));
	}
	else if (skilldiff
		&& skill_info[skillnum].classknow[ch_class][ch_kin] == KNOW_SKILL)
	{
		ch->set_skill(skillnum, 5);
		send_to_char(ch, "�� ������� ������ '%s'.\r\n", skill_name(skillnum));
		log("Add %s to %s (trigskillturn)trigvnum %d", skill_name(skillnum), GET_NAME(ch), vnum);
	}
}

void trg_skilladd(CHAR_DATA * ch, const ESkill skillnum, int skilldiff, int vnum)
{
	int skill = ch->get_trained_skill(skillnum);
	ch->set_skill(skillnum, (MAX(1, MIN(ch->get_trained_skill(skillnum) + skilldiff, 200))));

	if (skill > ch->get_trained_skill(skillnum))
	{
		send_to_char(ch, "���� ������ '%s' ����������.\r\n", skill_name(skillnum));
		log("Decrease %s to %s from %d to %d (diff %d)(trigskilladd) trigvnum %d", skill_name(skillnum), GET_NAME(ch), skill, ch->get_trained_skill(skillnum), skilldiff, vnum);
	}
	else if (skill < ch->get_trained_skill(skillnum))
	{
		send_to_char(ch, "�� �������� ���� ������ '%s'.\r\n", skill_name(skillnum));
		log("Raise %s to %s from %d to %d (diff %d)(trigskilladd) trigvnum %d", skill_name(skillnum), GET_NAME(ch), skill, ch->get_trained_skill(skillnum), skilldiff, vnum);
	}
	else
	{
		send_to_char(ch, "���� ������ �������� ���������� '%s '.\r\n", skill_name(skillnum));
		log("Unchanged %s on %s (trigskilladd) trigvnum %d", skill_name(skillnum), GET_NAME(ch), vnum);
	}
}

void trg_spellturn(CHAR_DATA * ch, int spellnum, int spelldiff, int vnum)
{
	int spell = GET_SPELL_TYPE(ch, spellnum);
	if (spell & SPELL_KNOW)
	{
		if (spelldiff) return;

		REMOVE_BIT(GET_SPELL_TYPE(ch, spellnum), SPELL_KNOW);
		if (!IS_SET(GET_SPELL_TYPE(ch, spellnum), SPELL_TEMP))
			GET_SPELL_MEM(ch, spellnum) = 0;
		send_to_char(ch, "�� ������� ������ ���������� '%s'.\r\n", spell_name(spellnum));
		log("Remove %s from %s (trigspell) trigvnum %d", spell_name(spellnum), GET_NAME(ch), vnum);
	}
	else if (spelldiff)
	{
		SET_BIT(GET_SPELL_TYPE(ch, spellnum), SPELL_KNOW);
		send_to_char(ch, "�� �������� ���������� '%s'.\r\n", spell_name(spellnum));
		log("Add %s to %s (trigspell) trigvnum %d", spell_name(spellnum), GET_NAME(ch), vnum);
	}
}

void trg_spelladd(CHAR_DATA * ch, int spellnum, int spelldiff, int vnum)
{
	int spell = GET_SPELL_MEM(ch, spellnum);
	GET_SPELL_MEM(ch, spellnum) = MAX(0, MIN(spell + spelldiff, 50));

	if (spell > GET_SPELL_MEM(ch, spellnum))
	{
		if (GET_SPELL_MEM(ch, spellnum))
		{
			log("Remove custom spell %s to %s (trigspell) trigvnum %d", spell_name(spellnum), GET_NAME(ch), vnum);
			sprintf(buf, "�� ������ ����� ���������� '%s'.\r\n", spell_name(spellnum));
		}
		else
		{
			sprintf(buf, "�� ������ ��� ���������� '%s'.\r\n", spell_name(spellnum));
			REMOVE_BIT(GET_SPELL_TYPE(ch, spellnum), SPELL_TEMP);
			log("Remove all spells %s to %s (trigspell) trigvnum %d", spell_name(spellnum), GET_NAME(ch), vnum);
		}
		send_to_char(buf, ch);
	}
	else if (spell < GET_SPELL_MEM(ch, spellnum))
	{
		if (!IS_SET(GET_SPELL_TYPE(ch, spellnum), SPELL_KNOW))
			SET_BIT(GET_SPELL_TYPE(ch, spellnum), SPELL_TEMP);
		send_to_char(ch, "�� ������� ��������� ���������� '%s'.\r\n", spell_name(spellnum));
		log("Add %s to %s (trigspell) trigvnum %d", spell_name(spellnum), GET_NAME(ch), vnum);
	}
}

void trg_spellitem(CHAR_DATA * ch, int spellnum, int spelldiff, int spell)
{
	char type[MAX_STRING_LENGTH];

	if ((spelldiff && IS_SET(GET_SPELL_TYPE(ch, spellnum), spell)) ||
			(!spelldiff && !IS_SET(GET_SPELL_TYPE(ch, spellnum), spell)))
		return;
	if (!spelldiff)
	{
		REMOVE_BIT(GET_SPELL_TYPE(ch, spellnum), spell);
		switch (spell)
		{
		case SPELL_SCROLL:
			strcpy(type, "�������� ������");
			break;
		case SPELL_POTION:
			strcpy(type, "������������� �������");
			break;
		case SPELL_WAND:
			strcpy(type, "������������ ������");
			break;
		case SPELL_ITEMS:
			strcpy(type, "���������� �����");
			break;
		case SPELL_RUNES:
			strcpy(type, "������������� ���");
			break;
		};
		sprintf(buf, "�� �������� ������ %s '%s'", type, spell_name(spellnum));
	}
	else
	{
		SET_BIT(GET_SPELL_TYPE(ch, spellnum), spell);
		switch (spell)
		{
		case SPELL_SCROLL:
			if (!ch->get_skill(SKILL_CREATE_SCROLL))
				ch->set_skill(SKILL_CREATE_SCROLL, 5);
			strcpy(type, "�������� ������");
			break;
		case SPELL_POTION:
			if (!ch->get_skill(SKILL_CREATE_POTION))
				ch->set_skill(SKILL_CREATE_POTION, 5);
			strcpy(type, "������������� �������");
			break;
		case SPELL_WAND:
			if (!ch->get_skill(SKILL_CREATE_WAND))
				ch->set_skill(SKILL_CREATE_WAND, 5);
			strcpy(type, "������������ ������");
			break;
		case SPELL_ITEMS:
			strcpy(type, "���������� �����");
			break;
		case SPELL_RUNES:
			strcpy(type, "������������� ���");
			break;
		};
		sprintf(buf, "�� ��������� ������ %s '%s'", type, spell_name(spellnum));
		send_to_char(buf, ch);
		check_recipe_items(ch, spellnum, spell, TRUE);
	}
}

// vim: ts=4 sw=4 tw=0 noet syntax=cpp :
