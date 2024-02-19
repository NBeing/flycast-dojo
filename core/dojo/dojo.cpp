#include "dojo.h"

void Dojo::AssignPlayerNames()
{
	hosting = config::ActAsServer;

	if (hosting)
	{
		player_1 = settings.dojo.PlayerName;
		player_2 = settings.dojo.OpponentName;
	}
	else
	{
		player_1 = settings.dojo.OpponentName;
		player_2 = settings.dojo.PlayerName;
	}
}
