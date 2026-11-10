local path='server/scripts/deathNotifications.lua'
local f=io.open(path);if f then f:close() else path='builder/ArenaMP_Builder/'..path end
config={deathNotificationDistance=2500}
local cell={[1]='Home A',[2]='Home A',[3]='Home A',[4]='Home B',[5]='Home A',[6]='Home A'}
local x={[1]=0,[2]=2499,[3]=2501,[4]=0,[5]=0,[6]=0}
local z={[1]=0,[2]=0,[3]=0,[4]=0,[5]=0,[6]=2501}
Players={}
for i=1,6 do Players[i]={accountName='Player'..i,language='RU',IsLoggedIn=function() return i~=5 end} end
local boxes={};local playerKiller=false;local killerPid=2
local function noChat() error('Death notification must not enter global or local chat') end
tes3mp={
 GetCell=function(i)return cell[i]end,GetPosX=function(i)return x[i]end,
 GetPosY=function(i)return 0 end,GetPosZ=function(i)return z[i]end,
 DoesPlayerHavePlayerKiller=function()return playerKiller end,
 GetPlayerKillerPid=function()return killerPid end,GetPlayerKillerName=function()return ''end,
 MessageBox=function(i,id,text)assert(id==-1);boxes[i]=text end,SendMessage=noChat}
local notices=dofile(path)
notices.Notify(1)
assert(boxes[1]=='Player1 погиб.' and boxes[2]=='Player1 погиб.')
assert(not boxes[3] and not boxes[4] and not boxes[5] and not boxes[6])
boxes={};playerKiller=true;killerPid=1;notices.Notify(1)
assert(boxes[2]:find('самоубийство',1,true) and not boxes[4])
boxes={};killerPid=2;Players[2].language='EN';notices.Notify(1)
assert(boxes[2]=='Player1 was killed by Player2.')
print('PASS: exact cell, horizontal/vertical distance, disconnected player, suicide, killer, MessageBox only')
