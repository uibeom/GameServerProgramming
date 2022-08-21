
myid = 99999;
escape = 3;

function set_uid(x)
   myid = x;
end

function event_player_move(player)
   player_x = API_get_x(player);
   player_y = API_get_y(player);
   my_x = API_get_x(myid);
   my_y = API_get_y(myid);
   if (player_x == my_x) then
      if (player_y == my_y) then
       escape = 3;
        API_SendMessage(myid, player, "KILL!!");
      end
   end
end

function event_npc_escape(player)
   if (escape > 0) then
       escape = escape - 1;
       return 0;
   else
       API_SendMessage(myid, player, "ESCAPE!!");
       return 1;
   end
end

function npc_data()
	name = "Cat";
    maxhp = 100;
	hp = 100;
	level = 1;
	return name, maxhp, hp, level;
end