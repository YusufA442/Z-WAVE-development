'Libraries and globals
Import mojo
Import brl
Import tfont

Global Game:Game_app

Global names:String[11]
Global playerscores:Int[11]

Global lbfont:TFont
Global path:String="C:\Users\Yusuf\Work\Z-WAVE - Coursework final version\Game files\shooter.data\scorefile.txt"

'Main program starts here:
Function Main ()
	Game = New Game_app
End

'All game code goes here:
Class Game_app Extends App
	Field player:Player
	Field raygunbullet_collection:List<RayGunBullet>
	Field enemy_collection:List<Zombie>
	Field menu:Image
	Field help:Image
	Field help2:Image
	Field help3:Image
	Field helpoverlay:Image
	
	Field room:Image
	Field endscreen:Image
	Field leaderboard:Image
	
	Field settings:Image ''' the final feature to add to make the game that the user never needs to debug the game (normies)
	
	Field pressed_p:Bool
	Field path_accessed:Bool
	Field path_given:String
	
	
	Field helpscreen:Int
	
	
	Field score:Int
	Field points:Int
	
	Field raygunbullet_count:Int
	
	Global GameState:String
	
	 
	Field round:Int 
	Field zombies_killed_in_round:Int
	Field zombies_in_round:Int 
	Field zombies_spawned_in_round:Int
	
	Field reload_state:String
	Field reload_cycle:Int
	Field health:Int 
	
	Field enemy_collision:Bool
	Field player_collision:Bool
	
	
	Field tick:Int
	Field tick_health:Int
	Field tick_reload:Int
	Field regenerate:Bool
	
	Field newRound_sound:Sound
	Field reload_sound:Sound
	Field shoot_sound:Sound
	'Field gameOver_sound:Sound
	'Field lobby_sound:Sound
	Field punch_sound:Sound
	Field knife_sound:Sound
	Field click_sound:Sound
	
	
	
	Field direction:Bool
	
	
	Field music_volume:Float
	Field music_track:String
	
	
	Field health_zombie:Int
	
	
	Field name:String
	Field name_given:String
	
	Field gun_damage:Int
	Field PaP_upgrades:Int
	Field weapon_damage:Int
	Field tick_PaP:Int
	Field weapon_upgradeCooldown:Bool
	Field PaP_cost:Int
	Field PaP_cycle:Int
	
	Field spawned_on_screen:Int
	Method OnCreate ()
		'All the initialisation for game (sounds, sprites, declaring variable amounts)
		player = New Player
		raygunbullet_collection = New List<RayGunBullet>
		enemy_collection = New List<Zombie>
		enemy_collection.AddLast(New Zombie)
		enemy_collection.AddLast(New Zombie)
		
		SetUpdateRate 60
	


		menu = LoadImage ("menu.png")
		help = LoadImage ("help.png")
		help2 = LoadImage ("howtoplay.png")
		help3 = LoadImage ("lasthelp.png")
		helpoverlay = LoadImage ("helpoverlay.png")
		
		room = LoadImage ("main room.png")
		endscreen = LoadImage ("lobby.png")
		leaderboard = LoadImage ("leaderboard.png")
		
		
		helpscreen=0
		
		
		tick=0
		tick_health =99999999
		tick_reload=99999999
		regenerate=False
		
		
		
		GameState= "MENU"
		
		score=0
		points=0
		
		zombies_killed_in_round =-2
		zombies_in_round=1
		zombies_spawned_in_round=0

		round=1
		reload_state="Yes"
		reload_cycle=2
		health=200
		raygunbullet_count=20
		
		enemy_collision=False
		player_collision=False
		
		reload_sound=LoadSound("reload.ogg")
		shoot_sound=LoadSound("shoot.ogg")
		newRound_sound=LoadSound("round change.ogg")
		punch_sound=LoadSound("punch.ogg")
		knife_sound=LoadSound("knife.ogg")
		click_sound=LoadSound("click.ogg")
		
		
		direction=True
		
		lbfont=New TFont("tbgold.ttf",25,[255,9,35])
		
		
		
		music_volume=1.0
		music_track="horde sounds"
		
		
		health_zombie=1
		
		gun_damage=5
		PaP_upgrades=0
		weapon_damage=gun_damage*(PaP_upgrades+1)
		tick_PaP=99999999
		weapon_upgradeCooldown=False
		PaP_cost=5000
		PaP_cycle=4
		
		spawned_on_screen=2 'two will always be on screen spawned so we put this to two and reset it to two every round as two spawn that are not part of zombies_in_round
	End






	Method OnUpdate ()
	'All the game logic
	
	

		Select GameState
			Case "MENU"
				If health<=0 Then GameState="Post Mortem"
				If music_track="horde sounds" Or music_track="game over" Then
					music_track="lobby sound"
					PlayMusic("lobby.ogg",1)
					ResumeMusic()
				End
				If KeyHit (KEY_SPACE) Then
					GameState="PLAYING"
					PauseMusic()
				End
		
				If KeyHit (KEY_H) Then GameState="HELP"
				If KeyHit (KEY_L) Then GameState="Leaderboard"
				If KeyHit (KEY_Q) Then EndApp
			Case "HELP"
				If health<=0 Then GameState="Post Mortem"
				If KeyHit (KEY_ESCAPE) Then GameState="MENU"
				If KeyHit (KEY_A) Then helpscreen-=1
				If KeyHit (KEY_D) Then helpscreen+=1
				If helpscreen>2 Then helpscreen=2
				If helpscreen<0 Then helpscreen=0
				'then this goes to code in OnRender
			Case "Leaderboard"
				If KeyHit(KEY_ESCAPE) Then GameState="MENU"
				toptenplayers()
		
			Case "Post Mortem"
				If score=0 Then score+=1
			
			
				If music_track="horde sounds" Or  music_track="lobby sound" Then
					PauseMusic()
					PlayMusic("game over.ogg",0)
					ResumeMusic()
					SetMusicVolume(music_volume)
					music_track="game over"
				End
			
			
			
				'only continue if string is right length
				While name.Length<10
					Local char=GetChar() ' declares characters
					If Not  char Exit
					If char>=10
						name+=String.FromChar(char)
					Endif
				Wend
				
				'delete char if backspace
				If KeyHit(KEY_BACKSPACE) Then
					If (name.Length>0) Then name=name[0..name.Length-1]
				End
				
				
				If KeyHit(KEY_ESCAPE) Then
					GameState="MENU"
					player=New Player
					raygunbullet_collection = New List<RayGunBullet>
					enemy_collection = New List<Zombie>
					enemy_collection.AddLast(New Zombie)
					enemy_collection.AddLast(New Zombie)
					
					tick=0
					tick_health=99999999
					tick_reload=99999999
					regenerate=False
					
					score=0
					points=0
					
					zombies_killed_in_round=-2
					zombies_in_round=1
					zombies_spawned_in_round=0
					
					round=1
					reload_state="Yes"
					reload_cycle=2
					health=200
					raygunbullet_count=20
					
					enemy_collision=False
					player_collision=False
					
					PlayMusic("lobby.ogg",1)
					SetMusicVolume(music_volume)
					ResumeMusic()
					
					health_zombie=1
					
					gun_damage=5
					PaP_upgrades=0
					tick_PaP=99999999
					weapon_upgradeCooldown=False
					weapon_damage=gun_damage*(PaP_upgrades+1)
					PaP_cost=5000
					PaP_cycle=0
					
					spawned_on_screen=2
				End
				
				
				If KeyHit(KEY_ENTER) Then
					writenamesandscore(name, score)
					GameState="MENU"
					player=New Player
					raygunbullet_collection = New List<RayGunBullet>
					enemy_collection = New List<Zombie>
					enemy_collection.AddLast(New Zombie)
					enemy_collection.AddLast(New Zombie)
					
					tick=0
					tick_health=99999999
					tick_reload=99999999
					regenerate=False
					
					score=0
					points=0
					
					zombies_killed_in_round=-2
					zombies_in_round=1
					zombies_spawned_in_round=0
					
					round=1
					reload_state="Yes"
					reload_cycle=2
					health=200
					raygunbullet_count=20
					
					enemy_collision=False
					player_collision=False
					
					PlayMusic("lobby.ogg",1)
					SetMusicVolume(music_volume)
					ResumeMusic()
					
					health_zombie=1
					
					gun_damage=5
					PaP_upgrades=0
					tick_PaP=99999999
					weapon_upgradeCooldown=False
					weapon_damage=gun_damage*(PaP_upgrades+1)
					PaP_cost=5000
					PaP_cycle=0
					
					spawned_on_screen=2
					
					
					'''reset all in-game stats after saving name to leaderboard so next player can play
				End
				
			Case "NewRound"
				If health<=0 Then GameState="Post Mortem"
				StopMusic()
				music_track="new round"
				PlaySound(newRound_sound,5)
				round+=1
				zombies_in_round=zombies_in_round+Rnd(4,10) 'change this value to make rounds longer or shorter
				If zombies_in_round<0 Then zombies_in_round=0 'check for zombies_in_round<0 because otherwise you have to kill negative zombies which is impossible
		'the zombies required to kill are negative and zombies in round is negative then 
				zombies_spawned_in_round=0
				zombies_killed_in_round =-2
				spawned_on_screen=2
				
				If round Mod 2 =0 Then health_zombie=health_zombie*4
				
				
				enemy_collection.AddLast(New Zombie)
				enemy_collection.AddLast(New Zombie)
				GameState="PLAYING"
				
				
		
				
				
		Case "PLAYING"
			If health<=0 Then GameState="Post Mortem"
		
			If tick<121 Then tick+=1
			If tick=121 Then tick=0
			
			If tick=tick_PaP And PaP_cycle<3 Then PaP_cycle+=1
			
			If weapon_upgradeCooldown=True And PaP_cycle>=3 Then
				weapon_upgradeCooldown=False
				tick_PaP=99999999
			End
		
			
			If points>=PaP_cost And KeyHit(KEY_P) And weapon_upgradeCooldown=False Then
				points-=PaP_cost
				PaP_upgrades+=1
				weapon_damage=15*(PaP_upgrades+1)
				tick_PaP=tick
				weapon_upgradeCooldown=True
				PaP_cost+=1000
				PlaySound(punch_sound,6)
				PaP_cycle=0
				
				'cool sidebuffs
				raygunbullet_count=0
			End
			
			If music_track="lobby sound" Or music_track="new round" Then
				PlayMusic("horde sounds.ogg",1)
				music_track="horde sounds"
				SetMusicVolume(music_volume) ''' make this toggleable (using the key M)
				ResumeMusic()
			End
			
			If KeyHit (KEY_ESCAPE) Then GameState="MENU"
			
			If raygunbullet_count<20 Then
				If KeyHit(KEY_SPACE) And weapon_upgradeCooldown=False And reload_cycle>=1 Then 'sfsd
					PlaySound(shoot_sound,0)
					raygunbullet_collection.AddLast(New RayGunBullet(player.x,player.y))
					raygunbullet_count+=1
				End
			End	
			
			
			If raygunbullet_count=20 And KeyHit(KEY_SPACE) Then PlaySound(click_sound,1)
				
				
			If raygunbullet_count=0 Then reload_state="No"
			If raygunbullet_count=20 Then reload_state="Yes" ' to tell the player on-screen to reload on the right side of the screen (out of the way of user)
			 
			
			If tick=tick_reload And reload_cycle<2 Then reload_cycle+=1 'now relies on how many times it has been equal so every second. it now takes 2 seconds to reload again.
			
		' the reload variable is used to test whether the player is allowed to reload or not, so I will make it so that the player can reload at any time but not
		' within a 2 second window of the last one
		
			If KeyDown(KEY_R) And reload_cycle>=2 And raygunbullet_count=0=False Then ' and i have used a greater than sign just in case it still increases past 2.
				raygunbullet_count=0
				reload_cycle=0
				tick_reload=tick
				PlaySound(reload_sound,2)
			End
		
		
		
		
			If tick=tick_health Then regenerate=True
			If regenerate= True And health<200 Then health+=1
			If health=200 Then
				regenerate=False
				tick_health=99999999
			End 
		
				
				
				
				
				
				'Handle raygunbullet movement
			For Local raygunbullet:=Eachin raygunbullet_collection
				raygunbullet.Move(GameState)
				If raygunbullet.y<0 Then raygunbullet_collection.Remove raygunbullet
				If raygunbullet.y>480-21 Then raygunbullet_collection.Remove raygunbullet 'updated values to stop border shooting and exploiting the bottom two spawns especially bottom right
				If raygunbullet.x<0 Then raygunbullet_collection.Remove raygunbullet
				If raygunbullet.x>640-10 Then raygunbullet_collection.Remove raygunbullet 'same issue corrected as line 405 but with offset value for x width of bullet instead of y height
			Next
				'Handle player Movement
			player.Move(GameState,tick)
			If KeyDown(KEY_D)=True And KeyDown(KEY_A)=False Or KeyDown(KEY_D)=False And KeyDown(KEY_A)=True Then
				If KeyDown(KEY_D) Then direction=True
				If KeyDown(KEY_A) Then direction=False
			End
			'Movement for enemies and collision detection
			'For Local enemy:=Eachin enemy_collection
			
			'the code below joined the for loop below 
			
				
			'Next  
			
			
	
	
			For Local enemy:=Eachin enemy_collection
			
			'the new code that was above before
				enemy_collision=False 'resets to default state
				player_collision=False 'resets to default state
				If intersects(enemy.x,enemy.y,22,27,enemy.x,enemy.y,22,27) Then enemy_collision=True
				If intersects(player.x,player.y,20,22,enemy.x,enemy.y,22,27) Then player_collision=True
				enemy.Move(player.give_x(),player.give_y(),GameState,enemy_collision,player_collision,tick)
			
			'the code that was here before
			
			
				If intersects(player.x,player.y,20,22,enemy.x,enemy.y,22,27) Then
				
					health-=1 'reduce life of player when hit, has regen: heals if not hit for 1 second (n upon death of enemy using Rnd(0,100)>n) maybe?? healthkits but is redundant due to regenerate
					If health<=0 Then GameState="Post Mortem"
					tick_health=tick
					regenerate=False
					
					If KeyHit(KEY_V) Then
						PlaySound(knife_sound,4)
						points+=10
						score+=10
						
						If enemy.dead(health_zombie,10)=True Then
							enemy_collection.Remove enemy ' now the player can knife if they get too close, also there is now a higher reward and a reason to want to take damage, make the player make risky descisions so it is difficult and so they die.
							points+=130 'give incentive to want to knife (instant kill and more points but you have to allow them to hit you first)
							score+=130
							zombies_killed_in_round +=1
							spawned_on_screen-=1
						End
					
					End
				
				
				End
					
					For Local raygunbullet:=Eachin raygunbullet_collection
						'not adding raygun bullet here because it would check every bullet here for every zombie (so if max train, it would do 600 checks a second vs 60 for bullets) 600 is overkill
						'and it would make no sense seeing as update rate is 60 so i won't check here otherwise it is checking *10 more often than normal which is bad for performance
						
						If intersects(raygunbullet.x,raygunbullet.y,10,21,enemy.x,enemy.y,22,27) Then
							points+=10
							score+=10
							If raygunbullet.health(PaP_upgrades)=True Then raygunbullet_collection.Remove raygunbullet
								If enemy.dead(health_zombie,weapon_damage)=True Then
									enemy_collection.Remove enemy 'check the zombie to see if it has been shot enough to have dead state=True
									points+=50
									score+=50
									zombies_killed_in_round +=1
									spawned_on_screen-=1
								End
						End
						
				  	Next
			Next
	
		
			If zombies_spawned_in_round<zombies_in_round And spawned_on_screen<10 Then 'code for spawning in zombies, it checks if the amount spawned in is less than the amount needed for the round
			'if the amount needed for the round is met (so they are both equal) then there should not be anymore spawned in.
			'if the amount needed for the round is not met, there will be additional spawned into the round to make it equal so that the if statement bewlow can be met
			
			enemy_collection.AddLast(New Zombie())
			zombies_spawned_in_round+=1
			spawned_on_screen+=1
			End 'you have to add an end statement after an if statement does multiple things. You have some degree of freedom of where to add it, I ususally put it just after to
			'visualise all the code that the specific if statement effects. You could add it after all the if statements that are in that block of code, but before
			'the use of a loop (count/condition). I add them all in next to their respective block, as it allows me to single out which one does what, and to prevent
			'declaration or unidentification (compile) errors. In this case, I know that this end is for this if statement and the tow a few lines below are respectively
			',and as commented, for ending the select case and ending the method
			
			
			
	
	
			If zombies_spawned_in_round= zombies_in_round And zombies_killed_in_round = zombies_in_round Then GameState="NewRound"
			
			
			
			
			
			If health<=0 Then GameState="Post Mortem"
		End 'ending the select case
		
		
	End 'ending the method

 

Method OnRender ()
'All the graphics drawing


	Select GameState
		Case "MENU"
			DrawImage menu, 0,0
		Case "HELP"
			If helpscreen=0 Then
				DrawImage help, 0,0
				DrawImage helpoverlay, 0,0
			End
			If helpscreen=1 Then
				DrawImage help2, 0,0
				DrawImage helpoverlay, 0,0
			End
			If helpscreen=2 Then
				DrawImage help3, 0,0
				DrawImage helpoverlay, 0,0
			End
		
		Case "NewRound"
			Cls 255, 255, 255
			SetColor 0,0,0
			DrawImage player.sprite, player.x, player.y
		
		Case "Leaderboard"
			Cls 0,0,0 'clear the screen
			DrawImage leaderboard, 0,0
			'loop to draw all
			For Local rank:=0 Until 10
				lbfont.DrawText(" "+(rank+1)+")    "+playerscores[rank] +" "+ names[rank],0,0+(rank*50))
			Next
		
		
		Case "Post Mortem"
			Cls 125,125,125
			SetColor 115,9,35
			DrawImage endscreen, 0,0
			SetColor 115,9,35
			DrawRect(1000,1000,1000,1000)
			SetColor(115,9,35)
			DrawText("FINAL SCORE:"+score,290,235)
			DrawImage player.sprite, player.x, player.y
			
			
			'SetFont = LoadImage("font_16.png",16,16,64)
			
			DrawText("In Memoriam (p.s way to go, loser):"+name_given + name,50,75)
			
		Case "settings"
			pressed_p=False
			If pressed_p=False Then
				Cls 125,125,125
				SetColor 0,0,0
				DrawImage settings, 0,0
				DrawRect(1000,1000,1000,1000)
				DrawText("Edit Path (P)",0,235)
			End
			If pressed_p=True Then
				DrawText("Enter path:"+path_given + path,50,75)
			End
				
		Case "PLAYING"
			Cls 255, 50, 12
			DrawImage room, 0,0
			SetColor 255,255,255
				DrawRect(0,0,1000,0)
				SetColor(255,255,225)
				DrawText("Points:"+points,0,300)
			
			SetColor 255,255,255
				DrawRect(100,0,115,0)
				SetColor(255,255,225)
				DrawText("Ammo:"+(20-raygunbullet_count),560,300)
			
			SetColor 255,255,255
				DrawRect(80,0,215,0)
				SetColor(255,255,225)
				DrawText("Reload?:"+reload_state,560,314)
			
			SetColor 255,255,255
				DrawRect(80,0,215,0)
				SetColor(255,255,225)
				DrawText("Health:"+health,0,314)
			
			SetColor 255,255,255
				DrawRect(80,0,215,0)
				SetColor(255,255,225)
				DrawText("Sprint:"+player.give_sprint_energy(),0,328)
			
			SetColor 255,255,255
				DrawRect(80,0,215,0)
				SetColor(255,255,225)
				DrawText("Round:"+round,100,0)
			
			SetColor 255,255,255
				DrawRect(80,0,215,0)
				SetColor(255,255,225)
				DrawText("Pack-A-Punch Upgrades:"+PaP_upgrades,200,0)
			
			SetColor 255,255,255
				DrawRect(80,0,215,0)
				SetColor(255,255,225)
				DrawText("Pack-A-Punch Cost:"+PaP_cost,400,0)
			
			
			If direction=True Then DrawImage player.sprite, player.x, player.y
			If direction=False Then DrawImage player.sprite2, player.x, player.y
			For Local enemy:=Eachin enemy_collection
				DrawImage enemy.sprite, enemy.x, enemy.y
				For Local raygunbullet:=Eachin raygunbullet_collection
					DrawImage raygunbullet.sprite, raygunbullet.x, raygunbullet.y
				Next
			Next
			
		End ' end select
		
	End ' end method OnRender

End ' end Game_app






Class Player
	Field sprite:Image = LoadImage ("player.png")
	Field sprite2:Image = LoadImage ("playerleft.png")
	Field x:Float = 300
	Field y:Float = 218
	Field Player_speed:Float = 1.2
	Field tick_sprint:Int=99999999
	Field sprint_energy:Int=500
	Field sprint_regeneration:Bool=True
	Field travel:Float=0
	Field footstep_sound:Sound=LoadSound("footstep.ogg")
	Field footstep1_sound:Sound=LoadSound("footstep1.ogg")
	Field footstep2_sound:Sound=LoadSound("footstep2.ogg")
	Field footstep3_sound:Sound=LoadSound("footstep3.ogg")
	Field footstep4_sound:Sound=LoadSound("footstep4.ogg")
	Field four_sidedCoin:Float = Rnd(0,4)
	Field huff_sound:Sound=LoadSound("huff.ogg")
	Field x_old:Float
	Field y_old:Float
	Field xspeed:Float=0
	Field yspeed:Float=0
	Field increm:Float=0.1
	Field sprint_multiplier:Float=2.0/1.2
	Field x_disp:Float
	Field y_disp:Float
	Field resultant_disp:Float
	Field up:Bool=False
	Field down:Bool=False
	Field left:Bool=False
	Field right:Bool=False


	Method Move(GameState:String,tick:Int)
		If KeyDown(KEY_SHIFT) And sprint_energy>0 And GameState = "PLAYING" And ( KeyDown(KEY_W)=True Or KeyDown(KEY_A)=True Or KeyDown(KEY_S)=True Or KeyDown(KEY_D)=True ) Then Player_speed=Player_speed*sprint_multiplier

		'precheck for diagonal
		If KeyDown(KEY_W) Then up=True
		If KeyDown(KEY_S) Then down=True
		If KeyDown(KEY_A) Then left=True
		If KeyDown(KEY_D) Then right=True
		
		If (left=True And right=True And down=True) Then
			right=False
			left=False
		End
		
		If (left=True And right=True And up=True) Then
			right=False
			left=False
		End
		
		If (up=True And down=True) Then
			down=False
			up=False
		End
		
		If (left=True And right=True) Then
			left=False
			right=False
		End
		
		
		
		If up=True And left=True Then (Player_speed) = (Player_speed) * 0.70711
		If up=True And right=True Then (Player_speed) = (Player_speed) * 0.70711
		If down=True And left=True Then (Player_speed) = (Player_speed) * 0.70711
		If down=True And right=True Then (Player_speed) = (Player_speed) * 0.70711
		
		If yspeed>Player_speed Then yspeed=Player_speed
		If yspeed<(-Player_speed) Then yspeed=(-Player_speed)
		If xspeed>Player_speed Then xspeed=Player_speed
		If xspeed<(-Player_speed) Then xspeed=(-Player_speed)
		
		x_old=x
		y_old=y
		
		If sprint_regeneration=True And sprint_energy<500 And GameState = "PLAYING" Then sprint_energy+=1
		
		If tick_sprint=tick And GameState = "PLAYING" Then
			tick_sprint=99999999
			sprint_regeneration=True
		End
		
		If KeyDown(KEY_SHIFT) And sprint_energy>0 And ( KeyDown(KEY_W)=True Or KeyDown(KEY_A)=True Or KeyDown(KEY_S)=True Or KeyDown(KEY_D)=True ) And GameState = "PLAYING" Then
			sprint_energy-=1
			sprint_regeneration=False
			tick_sprint=tick
		End
		
'was here before (sprint check)
		
		If up=True And yspeed>(-Player_speed) And GameState = "PLAYING" Then yspeed-=(increm)
		If left=True And xspeed>(-Player_speed) And GameState = "PLAYING" Then xspeed-=(increm)
		If down=True And yspeed<Player_speed And GameState = "PLAYING" Then yspeed+=(increm)
		If right=True And xspeed<Player_speed And GameState = "PLAYING" Then xspeed+=(increm)
		
		If ( up=False And down=False ) And (yspeed>0) Then yspeed-=increm
		If ( up=False And down=False ) And (yspeed<0) Then yspeed+=increm
		If ( left=False And right=False ) And (xspeed>0) Then xspeed-=increm
		If ( left=False And right=False ) And (xspeed<0) Then xspeed+=increm
		
		If (-increm<xspeed And xspeed<increm) Then xspeed=0 'if the value is too small to be able to minus, this is here to automatically default to zero because the resolution needed to subtract is
		If (-increm<yspeed And yspeed<increm) Then yspeed=0 'too high, so we just reset the value if it is within the range (-0.1,0.1) 
		
		y+=(yspeed)
		x+=(xspeed)
		
		If x>=620 Then x=620
		If x<=0 Then x=0
		If y>=458 Then y=458
		If y<=0 Then y=0
		
		
		x_disp=x-x_old
		y_disp=y-y_old
		
		resultant_disp = Pow( (x_disp*x_disp) + (y_disp*y_disp) , 0.5 )
		travel += resultant_disp
		
		If KeyDown(KEY_SHIFT) And GameState="PLAYING" And sprint_energy>0 And ( KeyDown(KEY_W) Or KeyDown(KEY_A) Or KeyDown(KEY_S) Or KeyDown(KEY_D) ) Then travel+=increm
		
		If travel>20 And GameState = "PLAYING" Then
			'four_sidedCoin=Rnd(0,4)
			'If 0<=four_sidedCoin And four_sidedCoin>1 Then PlaySound(footstep1_sound,8)
			'If 1<=four_sidedCoin And four_sidedCoin>2 Then PlaySound(footstep1_sound,8)
			'If 2<=four_sidedCoin And four_sidedCoin>3 Then PlaySound(footstep1_sound,8)
			'If 3<=four_sidedCoin And four_sidedCoin>4 Then PlaySound(footstep1_sound,8)
			PlaySound(footstep2_sound,8)
			travel=0
		End
		
		If KeyHit(KEY_SHIFT) And GameState = "PLAYING" Then
			PlaySound(huff_sound,9)
		
		End
		
		Player_speed=1.2
		up=False
		down=False
		left=False
		right=False
		
	End


	Method give_x() 'getter for x position of player for Zombie 'simple AI' seeking
		Return x
	End
	
	Method give_y() 'getter for y position of player for Zombie 'simple AI' seeking
		Return y
	End
	
	
	Method give_sprint_energy()
		Return sprint_energy
	End

End





Class Zombie
	Field sprite:Image = LoadImage ("zombie.png")
	Field x:Float
	Field y:Float
	
	Field ydistance:Float
	Field xdistance:Float
	Field Zombie_speed:Float =0.85
	
	Field x_Rnd:Float
	Field y_Rnd:Float
	
	Field damage_taken:Int =0
	
	Field angle:Float
	
	Field tick_tap:Int
	Field player_advantage:Bool

	Method New()
		
		
		x_Rnd=Rnd(0,1)
		y_Rnd=Rnd(0,1)
		
		If x_Rnd<0.5 Then x=Rnd(-600,-300)
		If x_Rnd>=0.5 Then x=Rnd(850,1200)
		
		If y_Rnd<0.5 Then y=Rnd(-700,-400)
		If y_Rnd>=0.5 Then y=Rnd(700,1200)
		
		If Rnd(0,1)>0.9 Then Zombie_speed=2.0 'This code is here to create super-sprinters and to resist grouping
	End


	Method Move(playerx:Float,playery:Float,GameState:String,enemy_collision:Bool,player_collision:Bool,tick:Int)
		
		angle= ATan2(((playery-5)-y), ((playerx-2)-x))  ' Calculate the angle between zombie and player
		
		If enemy_collision=True Then Zombie_speed=0.98 ' To make hording easier
	    If GameState = "PLAYING" And player_advantage=False Then 'This line now works for all states of positions unlike before with 4 if statements
	        x += Zombie_speed * Cos(angle) 'arcs the movement left or right if player is not in line on x plane
	    	y += Zombie_speed * Sin(angle) 'does the same but up and down in y plane
		End
	
		If tick_tap=tick And player_advantage=True Then player_advantage=False
		
		If player_collision=True Then
			If tick>-1 Then tick_tap=Rnd(30,60)
			If tick>30 Then tick_tap=Rnd(60,90)
			If tick>60 Then tick_tap=Rnd(90,120)
			If tick>90 Then tick_tap=Rnd(0,30)
			player_advantage=True
		End
		
		If enemy_collision=False Then Zombie_speed=1.1
	End




	Method dead(health_zombie:Int,damage_inflicted:Int)
		damage_taken+=damage_inflicted
		If damage_taken>=health_zombie Then Return(True)
	End

End 




Class RayGunBullet
	Field sprite:Image = LoadImage ("raygun.png")
	Field x:Float
	Field y:Float
	Field left:Bool
	Field right:Bool
	Field up:Bool
	Field down:Bool
	Field RayGunBullet_speed:Int = 10
	
	
	Field zombies_hit=0


	Method New(x_spawn:Float,y_spawn:Float)
		x = x_spawn+10
		y = y_spawn
		If KeyDown(KEY_W) Then up=True
		If KeyDown(KEY_S) Then down=True
		If KeyDown(KEY_A) Then left=True
		If KeyDown(KEY_D) Then right=True
		
		If up=False And left=False And down=False And right=False Then up = True
		
		If (left=True And right=True And down=True) Then
			right=False
			left=False
		End
		If (left=True And right=True And up=True) Then
			right=False
			left=False
		End
		If (up=True And down=True) Then down=False
			If (left=True And right=True) Then
				up=True
				left=False
				right=False
			End
		
		'final diagonal check below
		
		If up=True And left=True Then (RayGunBullet_speed) = (RayGunBullet_speed) * 0.70711
		
		If up=True And right=True Then (RayGunBullet_speed) = (RayGunBullet_speed) * 0.70711
		
		If down=True And left=True Then (RayGunBullet_speed) = (RayGunBullet_speed) * 0.70711
		
		If down=True And right=True Then (RayGunBullet_speed) = (RayGunBullet_speed) * 0.70711
		
		
		
	End 'end of method new for raygunbullet
	


	Method Move(GameState:String)
		
		
		If GameState="PLAYING" And up=True Then y-=(RayGunBullet_speed)
		
		If GameState="PLAYING" And down=True Then y+=(RayGunBullet_speed)
		
		If GameState="PLAYING" And left=True Then x-=(RayGunBullet_speed)
		
		If GameState="PLAYING" And right=True Then x+=(RayGunBullet_speed)
		
		
		'speed is set and forget so it should never be updated as to stop the bullet from changing speed. Its speed should be constant from the moment it is instantiated
		
		
	End


	Method health(PaP_upgrades:Int)
		zombies_hit+=1
		If zombies_hit>=(2+PaP_upgrades) Then Return(True)
	End

End



'DO NOT DELETE THE BELOW, intersects IS NOT A BUILT IN FUNCTION

Function intersects:Bool (x1:Int, y1:Int, w1:Int, h1:Int, x2:Int, y2:Int, w2:Int, h2:Int)
	If x1 >= (x2 + w2) Or (x1 + w1) <= x2 Then Return False
	If y1 >= (y2 + h2) Or (y1 + h1) <= y2 Then Return False
	Return True
End




Function writenamesandscore:Int(name:String,score:Int)
	Local score_file:FileStream
	Local score_data:String
	
	score_file=FileStream.Open(path,"a")
	score_data=String(score)
	score_file.WriteString(score_data+","+name+"~r~n")
	score_file.Close
	Return 1
End






Function toptenplayers() ' top ten function
	Local scores_file:FileStream 'declares file to be opened
	Local scores_data:String 'declares score data from score file as string
	Local scoresInt:Int 'declare scores as integer
	Local highscoreList:=New IntList 'declare the list for the top ten
	Local counter:Int 'declare counter for the for loop
	Local last:Int 'declare last score
	Local itemcount:Int 'declares item count
	'open file and read data
	scores_file= FileStream.Open(path,"r")
	scores_data= scores_file.ReadString()
	scores_file.Close
	'Loop to go through each score and add it to a list
	For Local eachscore:String = Eachin scores_data.Split("~n")
		scoresInt=Int(eachscore)
		highscoreList.AddLast scoresInt
	Next
	highscoreList.Sort(False) 'sorts list in descending order
	'Finds 10th score in the list and stores it as last variable
	counter=0
	For Local smallest=Eachin highscoreList
		If counter = 10 Then '10th score
			last=smallest
		End
		counter+= 1 'next score
	Next
	'loop to initalise arrays
	
	For Local pointer:=0 Until 10
		playerscores[pointer]=0
		names[pointer]=""
	Next
	counter=0
	'loop that makes the playerscores array and names arrays have the top 10 scores with the respective names
	For Local score:String=Eachin scores_data.Split("~n") 'Splits each line
		itemcount = 0 'initialise itemcount
		For Local item:String = Eachin score.Split(",") 'Splits each line
			'checks score above 10th score then add to the array
			If itemcount = 0 And Int(item) >= last Then
				playerscores[counter] = int(item) 'add score to array
			End
			If itemcount = 1 And playerscores[counter] >= last Then
				names[counter] = (item) 'add score to array
			End
			itemcount+= 1 'next item
		Next
		If playerscores[counter] >= last And counter < 10 Then
			counter+=1 'Next counter
		End
	Next
	InsertionSort() 'calls function
	Return 1
End



Function InsertionSort:Void() 'Function unsing insertion sort to sort names and scores
	For Local index:Int = 1 Until playerscores.Length ' Goes through each score
		Local value:Int = playerscores[index] ' declares score as value
		Local item:String = names[index] ' declares name as item
		Local previous:Int = index -1 'declares prevoius score index

		
		While ((previous >= 0) And (playerscores[previous] < value)) 'If previous score is less
			playerscores[previous + 1] = playerscores[previous] 'score moves to prev posiition as it is lower
			names[previous + 1] = names[previous] 'name moves to previous postition
			previous -=1
		Wend
		playerscores[previous+1] = value 'score is value
		names[previous +1]=item 'name is item
	Next
End