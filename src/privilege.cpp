// $RCSfile$     $Date$     $Revision$
// Copyright (c) 2007 Krodo
// Part of Bylins http://www.mud.ru

#include <boost/tokenizer.hpp>
#include <boost/algorithm/string.hpp>
#include "conf.h"
#include "sysdep.h"
#include "structs.h"
#include "utils.h"
#include "db.h"
#include "interpreter.h"
#include "boards.h"
#include "spells.h"
#include "privilege.hpp"

/**
* ������� ���������� ����� � ���������, ����������� � ������ god.lst.
* ������ �������������� �� ���� � ������ ���� ��������� -> ��� ��������� ������������ ������ ������� ����� �� �������.
* ��������� ������ ������ � ����� ��� ����� �������� ������������� ����������.
* ������ ����� privilege.lst:
* # ����������������� ������ (�������� �� ������): default, default_demigod, news, arena, skills
* # default - �������, ��������� ���� ����� �� ��������� (�������� �� � ����)
* # default_demigod - ������� ���� ��������� �� �������
* # news - ����������� ������ ������� �� ������ �����
* # arena - �������, ������� �������� ������ �� �����, ��� set � show (+ ������ ������� � ��������)
* # skills - ����, ����������� ���� ������������ ������������, ��������, ������, ���������� ����� (34� �� �������)
* # ��������� ������ ���������� ������� ������
* # ����: ��� ��� (0 �� ������, ������ ���� ����� ������ ���) �������/������
* <groups>
* default = wizhelp wiznet register ��� ����� title holylight uptime date set (title name) rules show nohassle ; show (punishment stats player)
* default_demigod = wizhelp wiznet ��� rules
* arena = purge
* olc = oedit zedit redit olc trigedit
* goto = goto ������ poofin poofout
* </groups>
* <gods>
* ���� 595336650 groups (olc) hell mute dumb ban delete set (bank)
* ���� 803863739 groups (arena goto olc news) hell mute dumb ban delete set (bank)
* </gods>
* ������ ����� ���������, zone.ru ��� �������� ����� �������� �� lua, � xml � ��������� ��� ������ �� ��������, ���� � ��������...
*/
namespace Privilege {

const int NEWS_MAKER = 0;
const int USE_SKILLS = 1;
const int ARENA_MASTER = 2;
// ���������� ������
const int FLAGS_NUM = 3;

typedef std::set<std::string> PrivListType;

class GodListNode {
	public:
	std::string name; // ���
	PrivListType set; // ��������� ���������� set
	PrivListType show; // ��������� ���������� show
	PrivListType other; // ��������� �������
	PrivListType arena; // ��������� ������� �� �����
	std::bitset<FLAGS_NUM> flags; // �����
	void clear()
	{
		name.clear();
		set.clear();
		show.clear();
		other.clear();
		arena.clear();
		flags.reset();
	}
};

const char *PRIVILEGE_FILE = LIB_MISC"privilege.lst";
typedef std::map<long, GodListNode> GodListType;
GodListType god_list; // �������� ������ ����� � ����������
std::map<std::string, std::string> group_list; // ��� ������, ������ ������ (��� ����� ��� ����� �����)
GodListNode tmp_god; // ��� �������
void parse_command_line(const std::string &command, int other_flags = 0); // ��������

/**
* ������ � ����� ���� ����� ����� (������ ���� ����� ���� � ������� ������������),
* ������� ����� ������������ �� ����� �� ������� �����.
* \param command - ��� �����
*/
void parse_flags(const std::string &command)
{
	if (command == "news")
		tmp_god.flags.set(NEWS_MAKER);
	else if (command == "skills")
		tmp_god.flags.set(USE_SKILLS);
	else if (command == "arena")
		tmp_god.flags.set(ARENA_MASTER);
}

/**
* ����������� ������� �� ������� (�����, set, show), �� ������ arena ������� ���� � ��������� ������
* \param command - ��� �������, fill_mode - � ����� ������ ���������, other_flags - ��� ����� ����� ����� �� ����� ������� ������
*/
void insert_command(const std::string &command, int fill_mode, int other_flags)
{
	if (other_flags == 1) {
		// � ����� ������� ������ ������ ������ ������, � �� ���� ����� �� ����� set ��� show
		if (!fill_mode)
			tmp_god.arena.insert(command);
		else
			return;
	}

	switch (fill_mode) {
	case 0:
		tmp_god.other.insert(command);
		break;
	case 1:
		tmp_god.set.insert(command);
		break;
	case 2:
		tmp_god.show.insert(command);
		break;
	case 3: {
		std::map<std::string, std::string>::const_iterator it = group_list.find(command);
		if (it != group_list.end()) {
			if (command == "arena")
				parse_command_line(it->second, 1);
			else
				parse_command_line(it->second);
		}
		break;
	}
	default:
		break;
	}
}

/**
* ���������� ����� � ��������� ������ ������ �� ��������� �� ����� default � default_demigod.
*/
void insert_default_command(long uid)
{
	std::map<std::string, std::string>::const_iterator it;
	if (get_level_by_unique(uid) < LVL_IMMORT)
		it = group_list.find("default_demigod");
	else
		it = group_list.find("default");
	if (it != group_list.end())
		parse_command_line(it->second);
}

/**
* ���� ������ ����� ������ � ������, ��� ���������� ���� ���� ����
* \param other_flags - �� ������� 0 (���������� ���� � �������� ������ ������), 1 - ���������� � ������ arena
* \param commands - ������ �� ������� ������ ��� �����
*/
void parse_command_line(const std::string &commands, int other_flags)
{
	typedef boost::tokenizer< boost::char_separator<char> > tokenizer;
	boost::char_separator <char>sep(" ", "()");
	tokenizer::iterator tok_iter, tmp_tok_iter;
	tokenizer tokens(commands, sep);

	std::string tmp_string;
	int fill_mode = 0;

	tokens.assign(commands);
	tok_iter = tokens.begin();
	if (tok_iter != tokens.end())
		tmp_string.assign(*tok_iter);
	else
		return;
	tmp_tok_iter = tok_iter;
	for (++tok_iter; tok_iter != tokens.end(); tok_iter++) {
		if ((*tok_iter) == "(") {
			if ((*tmp_tok_iter) == "set") {
				fill_mode = 1;
				continue;
			} else if ((*tmp_tok_iter) == "show") {
				fill_mode = 2;
				continue;
			} else if ((*tmp_tok_iter) == "groups") {
				fill_mode = 3;
				continue;
			}
		} else if ((*tok_iter) == ")") {
			fill_mode = 0;
			continue;
		}
		parse_flags(*tok_iter);
		insert_command(*tok_iter, fill_mode, other_flags);
		tmp_tok_iter = tok_iter;
	}
}

/**
* ���� � ������ ����� ���������� (reload privilege) � ����������� ������������� ��������� �����.
*/
void load() {
	std::ifstream file(PRIVILEGE_FILE);
	if (!file.is_open()) {
		log("Error open file: %s! (%s %s %d)", PRIVILEGE_FILE, __FILE__, __func__, __LINE__);
		return;
	}
	god_list.clear(); // ��� �������

	std::string name, commands, temp;
	long uid;

	while (file >> name) {
		if (name == "#") {
			ReadEndString(file);
			continue;
		} else if (name == "<groups>") {
			while (file >> name) {
				if (name == "</groups>")
					break;
				std::getline(file, commands);
				group_list[name] = commands;
				continue;
			}
			continue;
		} else if (name == "<gods>") {
			while (file >> name) {
				if (name == "</gods>")
					break;
				file >> uid;
				std::getline(file, commands);
				parse_command_line(commands);
				insert_default_command(uid);
				name_convert(name);
				tmp_god.name = name;
				god_list[uid] = tmp_god;
				tmp_god.clear();
			}
		}
	}
	// ������� ��������
	load_god_boards();
	group_list.clear();
}

/**
* ���� ���� � ������ �� ���� � ������� ���������� �����. ������� � CHAR_DATA �� ����� ����� �� �������������.
* ��� ��� ������ ���� �� wiz ������ ������� �� ������, ��� ������ ����� make test ����� � ���� ������ �� ������������.
* \param name - ��� ����, unique - ��� ���
* \return 0 - �� �����, 1 - �����
*/
bool god_list_check(const std::string &name, long unique)
{
#ifdef TEST_BUILD
	return 1;
#endif
	GodListType::const_iterator it = god_list.find(unique);
	if (it != god_list.end())
		if (it->second.name == name)
			return 1;
	return 0;
}

/**
* �������� � ����/������ ��������� �����.
*/
void load_god_boards()
{
	Board::clear_god_boards();
	for (GodListType::const_iterator god = god_list.begin(); god != god_list.end(); ++god) {
		int level = get_level_by_unique(god->first);
		if (level < LVL_IMMORT) continue;
		// ���� ��� ��� - ������ �������
		Board::init_god_board(god->first, god->second.name);
	}
}

/**
* �������� �� ����������� ������������� ������� (��� ������ � ������� 31+). 34� ���������� ��� �����������.
* \param mode 0 - ����� �������, 1 - ���������� set, 2 - ���������� show
* \return 0 - ������, 1 - �����
*/
bool can_do_priv(CHAR_DATA *ch, const std::string &cmd_name, int cmd_number, int mode)
{
	if (!mode && cmd_info[cmd_number].minimum_level < LVL_IMMORT && GET_LEVEL(ch) >= cmd_info[cmd_number].minimum_level)
		return true;
	if (IS_NPC(ch)) return false;
	GodListType::const_iterator it = god_list.find(GET_UNIQUE(ch));
	if (it != god_list.end() && CompareParam(it->second.name, GET_NAME(ch), 1)) {
		if (GET_LEVEL(ch) == LVL_IMPL) return true;
		switch (mode) {
		case 0:
			if (it->second.other.find(cmd_name) != it->second.other.end())
				return true;
			break;
		case 1:
			if (it->second.set.find(cmd_name) != it->second.set.end())
				return true;
			break;
		case 2:
			if (it->second.show.find(cmd_name) != it->second.show.end())
				return true;
			break;
		default:
			break;
		}
		// �� ����� �������� ������� �� ������ arena_master
		if (!mode && ROOM_FLAGGED(IN_ROOM(ch), ROOM_ARENA) && it->second.arena.find(cmd_name) != it->second.arena.end())
			return true;
	}
	return false;
}

/**
* �������� ������. 34� ������������� ������������� ������ skills
* ��� ����� �������� ������, �������� ��� ������������� ���.
* \param flag - ������ ������ � ������ �����, ���-�� FLAGS_NUM
* \return 0 - �� �����, 1 - �����
*/
bool check_flag(CHAR_DATA *ch, int flag)
{
	if (flag >= FLAGS_NUM || flag < 0) return false;
	bool result = false;
	GodListType::const_iterator it = god_list.find(GET_UNIQUE(ch));
	if (it != god_list.end() && CompareParam(it->second.name, GET_NAME(ch), 1))
		if (it->second.flags[flag])
			result = true;
	if (flag == USE_SKILLS && IS_IMPL(ch))
		result = true;
	return result;
}

/**
* �������� �� ����������� ����� ���������� �����.
* ������ skills ��� �����������. ������ arena ������ ������ � ����� � ������ �� ������� �����.
* � �������� � 34� �������� �� ������������.
*/
bool check_spells(CHAR_DATA *ch, int spellnum)
{
	// ���� use_skills - ����� � ��� ������
	if (!IS_IMMORTAL(ch) || IS_IMPL(ch) || check_flag(ch, USE_SKILLS))
		return true;
	// ���� arena_master - ������ �� ����� � ������ ��� �������/�����
	if (spellnum == SPELL_PORTAL || spellnum == SPELL_SUMMON)
		if (ROOM_FLAGGED(IN_ROOM(ch), ROOM_ARENA) && check_flag(ch, ARENA_MASTER))
			return true;
	return false;
}

/**
* �������� �� ����������� ������������� ������. ����� ����� get_skill.
* � �������� � 34� �������� �� ������������.
* \return 0 - �� ����� ������������ ������, 1 - �����
*/
bool check_skills(CHAR_DATA *ch, int skills)
{
	if (!IS_IMMORTAL(ch) || IS_IMPL(ch) || check_flag(ch, USE_SKILLS))
		return true;
	return false;
}

} // namespace Privilege
