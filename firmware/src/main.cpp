#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include <ESP32Servo.h>

#define TFT_CS    5
#define TFT_DC    2
#define TFT_RST   4
#define TFT_MOSI  23
#define TFT_SCLK  18

#define SERVO_PIN 13
#define BUZZER_PIN 27

WebServer server(80);
Preferences preferences;
Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);
Servo neck;
String currentMood = "idle";
unsigned long moodUntil = 0;
unsigned long lastDraw = 0;
String savedSSID, savedPassword;
String tasksData = "[]";
bool isInSetupMode = false;
bool focusMode = false;
bool breakMode = false;

unsigned long tabbieTimerEnd = 0;
unsigned long lastIdleAnimation = 0;
bool idleEyesOpen = true;
int idleLookOffset = 0;
int idleLookDirection = 1;
int servoPos = 90;

void startSetupMode();
void startNormalMode();
void setupWebServer();

void handleSetupPage();
void handleSaveWiFi();
void handleRoot();
void handleTasks();
void handleReset();
void handleStatus();
void handleMood();
void handleServo();
void handleTimer();
void handleRobotIcon();

void drawFace(String mood);
void drawRobotIcon(String type);
void drawIdleEyes(bool eyesOpen, int lookOffset);
void handleIdleAnimation();
void beep(int times);
void successSound();
void focusSound();
void reminderSound();
void updateRobot();

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  tft.begin();
  tft.setRotation(0);
  drawFace("idle");

  neck.attach(SERVO_PIN, 500, 2400);
  neck.write(servoPos);

  preferences.begin("tabbie", false);

  savedSSID = preferences.getString("ssid", "");
  savedPassword = preferences.getString("pass", "");
  tasksData = preferences.getString("tasks", "[]");

  if (savedSSID == "") startSetupMode();
  else startNormalMode();
}


void loop() {
  server.handleClient();
  updateRobot();
  if (focusMode || breakMode) {
    if (millis() > tabbieTimerEnd) {
      if (focusMode) {
        focusMode = false;
        breakMode = true;
        tabbieTimerEnd = millis() + (5UL * 60000UL);

        currentMood = "done";
        drawFace("done");
        successSound();

        Serial.println("Focus done. Break started.");
      } 
      else if (breakMode) {
        breakMode = false;

        currentMood = "idle";
        drawFace("idle");
        reminderSound();

        Serial.println("Break done.");
      }
    }
  }

  // Keep Tabbie alive: when no temporary icon/timer is active, eyes blink and move.
  if (!isInSetupMode && !focusMode && !breakMode && currentMood == "idle") {
    handleIdleAnimation();
  }
}

void startSetupMode() {
  isInSetupMode = true;

  WiFi.mode(WIFI_AP);
  WiFi.softAP("Tabbie-Setup");

  Serial.println("Setup Mode");
  Serial.print("Open: ");
  Serial.println(WiFi.softAPIP());

  drawFace("setup");
  setupWebServer();
}

void startNormalMode() {
  isInSetupMode = false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

  Serial.print("Connecting");

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("Tabbie IP: ");
    Serial.println(WiFi.localIP());

    drawFace("idle");
    setupWebServer();
  } else {
    Serial.println();
    Serial.println("WiFi failed. Back to setup.");
    startSetupMode();
  }
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    if (isInSetupMode) handleSetupPage();
    else handleRoot();
  });

  server.on("/save", HTTP_POST, handleSaveWiFi);

  server.on("/api/tasks", HTTP_GET, handleTasks);
  server.on("/api/tasks", HTTP_POST, handleTasks);

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/reset", HTTP_POST, handleReset);
  server.on("/api/mood", HTTP_POST, handleMood);
  server.on("/api/servo", HTTP_GET, handleServo);
  server.on("/api/servo", HTTP_POST, handleServo);
  server.on("/api/timer", HTTP_POST, handleTimer);

  server.on("/api/icon", HTTP_GET, handleRobotIcon);
  server.on("/api/icon", HTTP_POST, handleRobotIcon);
  server.on("/api/animation", HTTP_GET, handleRobotIcon);
  server.on("/api/animation", HTTP_POST, handleRobotIcon);
  server.on("/emoji", HTTP_GET, handleRobotIcon);
  server.on("/emoji", HTTP_POST, handleRobotIcon);

  server.begin();
  Serial.println("Server started");
}

void handleSetupPage() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Tabbie Setup</title>
<style>
body{margin:0;font-family:Arial;background:#0b1020;color:white;display:flex;align-items:center;justify-content:center;height:100vh}
.card{background:#151b2e;padding:26px;border-radius:22px;width:90%;max-width:420px;box-shadow:0 20px 60px #0008}
h1{text-align:center}
input,button{width:100%;box-sizing:border-box;padding:14px;margin:8px 0;border-radius:14px;border:0;font-size:16px}
button{background:#7c5cff;color:white;font-weight:bold}
</style>
</head>
<body>
<div class="card">
<h1>Tabbie Setup</h1>
<form action="/save" method="POST">
<input name="ssid" placeholder="WiFi Name" required>
<input name="pass" type="password" placeholder="WiFi Password">
<button type="submit">Save WiFi</button>
</form>
</div>
</body>
</html>
)rawliteral";

  server.send(200, "text/html; charset=utf-8", page);
}

void handleSaveWiFi() {
  preferences.putString("ssid", server.arg("ssid"));
  preferences.putString("pass", server.arg("pass"));

  server.send(200, "text/html; charset=utf-8", "<h1>Saved. Restarting...</h1>");
  delay(1200);
  ESP.restart();
}

void handleRoot() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Tabbie Robot</title>
<style>
:root{
  --card:#151b2e;
  --card2:#1d253b;
  --main:#7c5cff;
  --green:#00c853;
  --red:#ff5252;
  --yellow:#ffd54f;
  --orange:#ff9800;
  --blue:#2196f3;
  --text:#fff;
  --muted:#9da8c7;
}
*{box-sizing:border-box}
body{margin:0;font-family:Arial;background:linear-gradient(160deg,#090d18,#121a32);color:var(--text)}
.app{max-width:680px;margin:auto;padding:18px}
.header{background:var(--card);border-radius:26px;padding:22px;text-align:center;box-shadow:0 20px 60px #0007}
.robot{font-size:76px;line-height:1;margin:10px 0}
.status{color:var(--muted)}
.grid{display:grid;grid-template-columns:repeat(5,1fr);gap:10px;margin-top:14px}
.stat{background:var(--card2);border-radius:18px;padding:12px}
.stat b{display:block;font-size:24px}
.tabs{display:flex;gap:8px;margin:16px 0}
.tabs button{flex:1;background:var(--card2)}
.tabs button.active{background:var(--main)}
.panel{display:none;background:var(--card);border-radius:24px;padding:18px;box-shadow:0 12px 40px #0005}
.panel.active{display:block}
input,button,select{padding:13px;border:0;border-radius:14px;font-size:15px}
input,select{background:#eef1ff;width:100%}
button{background:var(--main);color:white;font-weight:bold;cursor:pointer}
.row{display:flex;gap:8px;margin:10px 0}
.row input{flex:1}
.timeBox{max-width:130px}
.priorityBox{max-width:135px}
.filterRow{display:grid;grid-template-columns:repeat(4,1fr);gap:8px;margin:12px 0}
.filterRow button{background:var(--card2);font-size:13px;padding:11px}
.filterRow button.active{background:var(--main)}
ul{padding:0;margin:0}
li{list-style:none;background:var(--card2);margin:9px 0;padding:12px;border-radius:16px;display:flex;justify-content:space-between;align-items:center;gap:8px;border-left:7px solid transparent}
li.high{border-left-color:var(--red)}
li.medium{border-left-color:var(--orange)}
li.low{border-left-color:var(--green)}
li.doneTask{opacity:.6}
.taskText{flex:1;text-align:left;cursor:pointer;line-height:1.4}
.taskTitle{font-weight:bold}
.taskMeta{display:flex;gap:7px;flex-wrap:wrap;margin-top:7px}
.badge{font-size:12px;padding:4px 8px;border-radius:99px;background:#10162a;color:white}
.badgeTime{background:#2b3554;color:#ffd54f}
.badgeHigh{background:#5c1720;color:#ffb3b3}
.badgeMedium{background:#53390b;color:#ffe0a3}
.badgeLow{background:#0e4d2a;color:#a5f5c4}
.badgeDone{background:#24482d;color:#a5f5c4}
.done{text-decoration:line-through;color:var(--muted)}
.actionBtns{display:flex;gap:6px;align-items:center;flex-wrap:wrap;justify-content:flex-end}
.smallBtn{padding:9px 10px;border-radius:12px;font-size:13px}
.startBtn{background:#00a86b}
.doneBtn{background:#00c853}
.skipBtn{background:#777}
.workTime{background:#18213a;color:#b6c7ff}
.badgeProgress{background:#203d66;color:#b8dcff}
.badgeSkipped{background:#4a4a4a;color:#ddd}
.red{background:var(--red)}
.green{background:var(--green)}
.yellow{background:#f9a825}
.blue{background:var(--blue)}
.timer{font-size:54px;font-weight:bold;margin:16px;text-align:center}
.quick button{width:100%;margin:6px 0}
.empty{color:var(--muted);text-align:center;padding:18px}
@media(max-width:560px){
  .row{flex-direction:column}
  .timeBox,.priorityBox{max-width:none}
  .filterRow{grid-template-columns:1fr 1fr}
  .grid{grid-template-columns:1fr 1fr}
}
</style>
</head>

<body>
<div class="app">

  <div class="header">
    <h1>Tabbie Desk Robot</h1>
    <div class="robot" id="face">🙂</div>
    <div class="status" id="mood">Ready to work</div>
    <div class="timer" id="topTimer">25:00</div>

    <div class="grid">
      <div class="stat"><b id="totalCount">0</b><span>Tasks</span></div>
      <div class="stat"><b id="doneCount">0</b><span>Done</span></div>
      <div class="stat"><b id="focusCount">0</b><span>Focus</span></div>
      <div class="stat"><b id="progressCount">0%</b><span>Progress</span></div>
      <div class="stat"><b id="streakCount">0</b><span>Streak</span></div>
    </div>
    <div class="status" id="productivityStatus">Starting...</div>
  </div>

  <div class="tabs">
    <button class="active" onclick="openTab('tasks',this)">Tasks</button>
    <button onclick="openTab('focus',this)">Focus</button>
    <button onclick="openTab('robot',this)">Robot</button>
  </div>

  <div id="tasks" class="panel active">
    <h2>Professional Tasks</h2>

    <div class="row">
      <input id="taskInput" placeholder="Add important task">
      <input id="taskTime" class="timeBox" type="time" onchange="showRobotIcon('date')">
      <select id="taskPriority" class="priorityBox">
        <option value="high">High</option>
        <option value="medium" selected>Medium</option>
        <option value="low">Low</option>
      </select>
      <button onclick="addTask()">Add</button>
    </div>

    <div class="filterRow">
      <button id="filterAll" class="active" onclick="setFilter('all')">All</button>
      <button id="filterPending" onclick="setFilter('pending')">Pending</button>
      <button id="filterDone" onclick="setFilter('done')">Done</button>
      <button id="filterHigh" onclick="setFilter('high')">High</button>
    </div>

    <ul id="taskList"></ul>
  </div>

  <div id="focus" class="panel">
    <h2>Focus Timer</h2>
    <select id="focusMode" onchange="setMode()">
      <option value="25">Pomodoro 25 min</option>
      <option value="15">Short focus 15 min</option>
      <option value="5">Break 5 min</option>
      <option value="1">Test 1 min</option>
    </select>

    <div class="timer" id="timerText">25:00</div>

    <div class="row">
      <button class="green" onclick="startTimer()">Start</button>
      <button class="yellow" onclick="pauseTimer()">Pause</button>
      <button class="red" onclick="resetTimer()">Reset</button>
    </div>
  </div>

  <div id="robot" class="panel">
    <h2>Robot Mood</h2>
    <div class="quick">
      <button onclick="setMood('🙂','Ready to work','idle')">Idle</button>
      <button onclick="setMood('😐','Focus mode','focus')">Focus</button>
      <button onclick="setMood('😴','Break time','break')">Break</button>
      <button onclick="setMood('🎉','Great job!','done')">Celebrate</button>
      <button onclick="setMood('⚠️','Tasks waiting','reminder')">Reminder</button>
    </div>

    <h2>Servo</h2>
    <input type="range" min="0" max="180" value="90" oninput="moveServo(this.value)">
    <button class="blue" onclick="moveServo(90)">Center</button>

    <h2>Settings</h2>
    <button class="red" onclick="resetWifi()">Reset WiFi</button>
  </div>

</div>

<script>
let tasks=[];
let timer=null;
let seconds=25*60;
let currentFilter='all';
let focusSessions=Number(localStorage.getItem('focusSessions')||0);
let lastSmartMood='';
let lastSmartMoodTime=0;
let completedCelebratedToday=localStorage.getItem('completedCelebratedToday') || '';
let streak=Number(localStorage.getItem('streak')||0);
let lastStreakDay=localStorage.getItem('lastStreakDay') || '';

function speak(text){
  if('speechSynthesis' in window){
    speechSynthesis.cancel();
    let msg=new SpeechSynthesisUtterance(text);
    msg.lang='en-US';
    msg.rate=0.95;
    msg.pitch=1;
    speechSynthesis.speak(msg);
  }
}

function showRobotIcon(type){
  fetch('/api/icon?type='+encodeURIComponent(type),{method:'GET'})
    .catch(()=>{});
}

function openTab(id,btn){
  document.querySelectorAll('.panel').forEach(p=>p.classList.remove('active'));
  document.querySelectorAll('.tabs button').forEach(b=>b.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  btn.classList.add('active');
}

function setMood(face,text,mood,voice=''){
  document.getElementById('face').innerHTML=face;
  document.getElementById('mood').innerHTML=text;
  fetch('/api/mood?mood='+mood,{method:'POST'});
  if(voice) speak(voice);
}

function loadTasks(){
  fetch('/api/tasks')
    .then(r=>r.json())
    .then(d=>{
      tasks=Array.isArray(d)?d:[];

      tasks.forEach(t=>{
        if(typeof t.time==='undefined') t.time='';
        if(typeof t.lastAlert==='undefined') t.lastAlert='';
        if(typeof t.priority==='undefined') t.priority='medium';
        if(typeof t.createdAt==='undefined') t.createdAt=Date.now();

        // New professional tracking fields
        if(typeof t.state==='undefined'){
          t.state = t.done ? 'done' : 'not_started';
        }
        if(typeof t.workSeconds==='undefined') t.workSeconds=0;
        if(typeof t.startedAt==='undefined') t.startedAt=0;
        if(typeof t.skipped==='undefined') t.skipped=false;
      });

      sortTasks();
      renderTasks();
    })
    .catch(()=>{
      tasks=[];
      renderTasks();
    });
}

function saveTasks(){
  fetch('/api/tasks',{
    method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'data='+encodeURIComponent(JSON.stringify(tasks))
  });
}

function addTask(){
  let input=document.getElementById('taskInput');
  let timeInput=document.getElementById('taskTime');
  let priorityInput=document.getElementById('taskPriority');

  let text=input.value.trim();
  let time=timeInput.value;
  let priority=priorityInput.value;

  if(!text)return;

  tasks.push({
    text:text,
    time:time,
    priority:priority,
    done:false,
    state:'not_started',
    skipped:false,
    workSeconds:0,
    startedAt:0,
    lastAlert:'',
    createdAt:Date.now()
  });

  input.value='';
  timeInput.value='';
  priorityInput.value='medium';

  sortTasks();
  renderTasks();
  saveTasks();

  if(priority==='high'){
    setMood('⚠️','High priority task added','reminder','High priority task added');
    showRobotIcon('add');
  }else if(time){
    setMood('🙂','Timed task added','idle','Timed task added');
    showRobotIcon('date');
  }else{
    setMood('🙂','New task added','idle','New task added');
    showRobotIcon('add');
  }
}

function toggleTask(i){
  if(tasks[i].state==='done'){
    reopenTask(i);
  }else{
    markDone(i);
  }
}

function startTask(i){
  let t=tasks[i];

  if(t.state==='done' || t.state==='skipped') return;

  // Stop any other running task first
  tasks.forEach((task,idx)=>{
    if(idx!==i && task.state==='in_progress'){
      stopTaskTimer(task);
      task.state='not_started';
    }
  });

  t.state='in_progress';
  t.done=false;
  t.skipped=false;
  t.startedAt=Date.now();

  sortTasks();
  renderTasks();
  saveTasks();

  setMood('😐','Task started','focus','Task started. Focus now');
  showRobotIcon('start');
}

function stopTaskTimer(t){
  if(t.startedAt && t.state==='in_progress'){
    let diff=Math.floor((Date.now()-t.startedAt)/1000);
    if(diff>0) t.workSeconds += diff;
    t.startedAt=0;
  }
}

function markDone(i){
  let t=tasks[i];

  stopTaskTimer(t);

  t.done=true;
  t.skipped=false;
  t.state='done';

  sortTasks();
  renderTasks();
  saveTasks();

  setMood('🎉','Task completed!','done','Task completed. Good job');
  showRobotIcon('done');
}

function skipTask(i){
  let t=tasks[i];

  stopTaskTimer(t);

  t.done=false;
  t.skipped=true;
  t.state='skipped';

  sortTasks();
  renderTasks();
  saveTasks();

  setMood('😴','Task skipped','break','Task skipped');
  showRobotIcon('break');
}

function reopenTask(i){
  let t=tasks[i];

  t.done=false;
  t.skipped=false;
  t.state='not_started';
  t.startedAt=0;

  sortTasks();
  renderTasks();
  saveTasks();

  setMood('🙂','Task reopened','idle','Task reopened');
}

function changePriority(i){
  let current=tasks[i].priority || 'medium';

  if(current==='high') tasks[i].priority='medium';
  else if(current==='medium') tasks[i].priority='low';
  else tasks[i].priority='high';

  sortTasks();
  renderTasks();
  saveTasks();
  speak('Priority changed');
}

function deleteTask(i){
  tasks.splice(i,1);
  renderTasks();
  saveTasks();
  speak('Task deleted');
}

function setFilter(filter){
  currentFilter=filter;

  document.querySelectorAll('.filterRow button').forEach(b=>b.classList.remove('active'));

  if(filter==='all') document.getElementById('filterAll').classList.add('active');
  if(filter==='pending') document.getElementById('filterPending').classList.add('active');
  if(filter==='done') document.getElementById('filterDone').classList.add('active');
  if(filter==='high') document.getElementById('filterHigh').classList.add('active');

  renderTasks();
}

function getFilteredTasks(){
  return tasks
    .map((t,i)=>({task:t,index:i}))
    .filter(item=>{
      let t=item.task;

      if(currentFilter==='pending') return !t.done && t.state!=='skipped';
      if(currentFilter==='done') return t.done;
      if(currentFilter==='high') return t.priority==='high' && !t.done && t.state!=='skipped';

      return true;
    });
}

function renderTasks(){
  let list=document.getElementById('taskList');
  list.innerHTML='';

  let visible=getFilteredTasks();

  if(visible.length===0){
    list.innerHTML='<div class="empty">No tasks here</div>';
    updateStats();
    return;
  }

  visible.forEach(item=>{
    let t=item.task;
    let i=item.index;

    let priority=t.priority || 'medium';
    let priorityText=priority.toUpperCase();
    let priorityClass='badgeMedium';

    if(priority==='high') priorityClass='badgeHigh';
    if(priority==='low') priorityClass='badgeLow';

    let state=t.state || (t.done ? 'done' : 'not_started');

    let stateText='TODO';
    let stateClass='';

    if(state==='in_progress'){
      stateText='IN PROGRESS';
      stateClass='badgeProgress';
    }else if(state==='done'){
      stateText='DONE';
      stateClass='badgeDone';
    }else if(state==='skipped'){
      stateText='SKIPPED';
      stateClass='badgeSkipped';
    }

    let timeHtml=t.time ? `<span class="badge badgeTime">⏰ ${t.time}</span>` : '';
    let doneHtml=`<span class="badge ${stateClass}">${stateText}</span>`;
    let workHtml=`<span class="badge workTime">⏱ ${formatWorkTime(getLiveWorkSeconds(t))}</span>`;

    let startDisabled=(state==='done' || state==='skipped') ? 'style="opacity:.45"' : '';
    let doneDisabled=(state==='done') ? 'style="opacity:.45"' : '';
    let skipDisabled=(state==='done' || state==='skipped') ? 'style="opacity:.45"' : '';

    list.innerHTML += `
      <li class="${priority} ${t.done?'doneTask':''} ${state==='skipped'?'doneTask':''}">
        <span class="taskText ${t.done?'done':''}" onclick="toggleTask(${i})">
          <span class="taskTitle">${escapeHtml(t.text)}</span>
          <span class="taskMeta">
            ${doneHtml}
            <span class="badge ${priorityClass}">${priorityText}</span>
            ${timeHtml}
            ${workHtml}
          </span>
        </span>

        <span class="actionBtns">
          <button class="smallBtn startBtn" ${startDisabled} onclick="event.stopPropagation();startTask(${i})">Start</button>
          <button class="smallBtn doneBtn" ${doneDisabled} onclick="event.stopPropagation();markDone(${i})">Done</button>
          <button class="smallBtn skipBtn" ${skipDisabled} onclick="event.stopPropagation();skipTask(${i})">Skip</button>
          <button class="smallBtn blue" onclick="event.stopPropagation();changePriority(${i})">P</button>
          <button class="smallBtn red" onclick="event.stopPropagation();deleteTask(${i})">X</button>
        </span>
      </li>
    `;
  });

  updateStats();
}

function getLiveWorkSeconds(t){
  let total=t.workSeconds || 0;

  if(t.state==='in_progress' && t.startedAt){
    total += Math.floor((Date.now()-t.startedAt)/1000);
  }

  return total;
}

function formatWorkTime(sec){
  sec=Math.max(0,sec||0);

  let h=Math.floor(sec/3600);
  let m=Math.floor((sec%3600)/60);
  let s=sec%60;

  if(h>0){
    return h + 'h ' + m + 'm';
  }

  if(m>0){
    return m + 'm ' + s.toString().padStart(2,'0') + 's';
  }

  return s + 's';
}

function updateStats(){
  let total=tasks.length;
  let done=tasks.filter(t=>t.done || t.state==='done').length;
  let skipped=tasks.filter(t=>t.state==='skipped').length;
  let inProgress=tasks.filter(t=>t.state==='in_progress').length;
  let activeTotal=tasks.filter(t=>t.state!=='skipped').length;
  let pending=tasks.filter(t=>!t.done && t.state!=='skipped').length;
  let highPending=tasks.filter(t=>t.priority==='high' && !t.done && t.state!=='skipped').length;

  let totalWorkSeconds=tasks.reduce((sum,t)=>sum+getLiveWorkSeconds(t),0);
  let percent=activeTotal ? Math.round((done/activeTotal)*100) : 0;

  document.getElementById('totalCount').innerHTML=total;
  document.getElementById('doneCount').innerHTML=done;
  document.getElementById('focusCount').innerHTML=focusSessions;
  document.getElementById('progressCount').innerHTML=percent + '%';
  document.getElementById('streakCount').innerHTML=streak;

  let status='Starting...';

  if(total===0){
    status='📝 Add your first task';
  }else if(inProgress>0){
    status='🎯 Working now — ' + formatWorkTime(totalWorkSeconds);
  }else if(percent===0){
    status='😴 Lazy start';
  }else if(percent<50){
    status='🙂 Keep going';
  }else if(percent<80){
    status='💪 Good progress';
  }else if(percent<100){
    status='🔥 Almost done';
  }else{
    status='🚀 Beast mode!';
  }

  if(highPending>0 && percent<100){
    status += ' — ' + highPending + ' high priority';
  }

  if(skipped>0){
    status += ' — ' + skipped + ' skipped';
  }

  document.getElementById('productivityStatus').innerHTML=status;

  smartBehavior(percent, highPending, pending, activeTotal);
  updateStreak(percent,activeTotal);
}

function todayKey(){
  let now=new Date();
  return now.getFullYear() + '-' +
    (now.getMonth()+1).toString().padStart(2,'0') + '-' +
    now.getDate().toString().padStart(2,'0');
}

function smartMoodAllowed(key){
  let now=Date.now();

  if(lastSmartMood===key && now-lastSmartMoodTime<120000){
    return false;
  }

  lastSmartMood=key;
  lastSmartMoodTime=now;
  return true;
}

function smartBehavior(percent, highPending, pending, total){
  if(total===0) return;

  let today=todayKey();

  if(percent===100 && completedCelebratedToday!==today){
    completedCelebratedToday=today;
    localStorage.setItem('completedCelebratedToday',today);

    setTimeout(()=>{
      setMood('🎉','All tasks done!','done','All tasks completed. Amazing work');
    },700);
    return;
  }

  if(highPending>0 && percent<50 && smartMoodAllowed('high-low-progress')){
    setTimeout(()=>{
      setMood('⚠️','High priority tasks waiting','reminder','You have important tasks to finish');
    },2500);
    return;
  }

  if(percent===0 && total>=3 && smartMoodAllowed('lazy-start')){
    setTimeout(()=>{
      setMood('😐','Start one task','focus','Start with one small task');
    },3000);
    return;
  }

  if(percent>=70 && percent<100 && pending>0 && smartMoodAllowed('good-progress')){
    setTimeout(()=>{
      setMood('😐','Keep pushing','focus','You are doing great. Keep going');
    },2500);
  }
}

function updateStreak(percent,total){
  let today=todayKey();

  if(total===0) return;

  if(percent===100 && lastStreakDay!==today){
    streak++;
    lastStreakDay=today;

    localStorage.setItem('streak',streak);
    localStorage.setItem('lastStreakDay',today);

    document.getElementById('streakCount').innerHTML=streak;
  }
}

function priorityScore(priority){
  if(priority==='high') return 1;
  if(priority==='medium') return 2;
  if(priority==='low') return 3;
  return 2;
}

function sortTasks(){
  tasks.sort((a,b)=>{
    let sa=a.state || (a.done ? 'done' : 'not_started');
    let sb=b.state || (b.done ? 'done' : 'not_started');

    // In progress first
    if(sa==='in_progress' && sb!=='in_progress') return -1;
    if(sa!=='in_progress' && sb==='in_progress') return 1;

    // Skipped and done go down
    let aFinished=(sa==='done' || sa==='skipped' || a.done);
    let bFinished=(sb==='done' || sb==='skipped' || b.done);
    if(aFinished !== bFinished) return aFinished ? 1 : -1;

    // Done before skipped inside finished area
    if(aFinished && bFinished && sa!==sb){
      if(sa==='done') return -1;
      if(sb==='done') return 1;
    }

    let pa=priorityScore(a.priority);
    let pb=priorityScore(b.priority);
    if(pa !== pb) return pa - pb;

    if(a.time && b.time) return a.time.localeCompare(b.time);
    if(a.time && !b.time) return -1;
    if(!a.time && b.time) return 1;

    return (a.createdAt || 0) - (b.createdAt || 0);
  });
}

function checkTaskReminders(){
  let now=new Date();

  let current=
    now.getHours().toString().padStart(2,'0') + ':' +
    now.getMinutes().toString().padStart(2,'0');

  let today=
    now.getFullYear() + '-' +
    (now.getMonth()+1).toString().padStart(2,'0') + '-' +
    now.getDate().toString().padStart(2,'0');

  let changed=false;

  tasks.forEach(t=>{
    if(!t.done && t.state!=='skipped' && t.time && t.time===current && t.lastAlert!==today){
      t.lastAlert=today;
      changed=true;

      if(t.priority==='high'){
        setMood('⚠️','High priority task time!','reminder','High priority task time now. ' + t.text);
        showRobotIcon('reminder');
      }else{
        setMood('⚠️','Task time!','reminder','Task time now. ' + t.text);
        showRobotIcon('reminder');
      }
    }
  });

  if(changed){
    saveTasks();
    renderTasks();
  }
}

function setMode(){
  let m=Number(document.getElementById('focusMode').value);
  seconds=m*60;
  showTimer();
}

function showTimer(){
  let m=Math.floor(seconds/60);
  let s=seconds%60;
  let txt=m+':'+(s<10?'0':'')+s;

  document.getElementById('timerText').innerHTML=txt;
  document.getElementById('topTimer').innerHTML=txt;
}

function startTimer(){
  if(timer)return;

  let m=Number(document.getElementById('focusMode').value);
  fetch('/api/timer?mode=start&minutes='+m,{method:'POST'});

  setMood('😐','Focus mode running','focus','Focus started');
  showRobotIcon('focus');

  timer=setInterval(()=>{
    seconds--;
    showTimer();

    if(seconds==60){
      speak('One minute remaining');
    }

    if(seconds<=0){
      clearInterval(timer);
      timer=null;

      focusSessions++;
      localStorage.setItem('focusSessions',focusSessions);
      updateStats();

      setMood('🎉','Focus done! Break started','done','Focus session done');
      showRobotIcon('done');

      seconds=5*60;
      showTimer();

      setTimeout(()=>{
        setMood('😴','Break time','break','Break time');
        showRobotIcon('break');
      },1500);
    }
  },1000);
}

function pauseTimer(){
  if(timer){
    clearInterval(timer);
    timer=null;
    fetch('/api/timer?mode=stop',{method:'POST'});
    setMood('🙂','Timer paused','idle','Timer paused');
  }
}

function resetTimer(){
  if(timer){
    clearInterval(timer);
    timer=null;
  }

  fetch('/api/timer?mode=stop',{method:'POST'});
  setMode();
  setMood('🙂','Timer reset','idle','Timer reset');
}

function moveServo(v){
  fetch('/api/servo?angle='+v,{method:'POST'});
}

function resetWifi(){
  if(confirm('Reset WiFi settings?')){
    speak('Resetting WiFi');
    fetch('/api/reset',{method:'POST'});
  }
}

function escapeHtml(text){
  return text.replace(/[&<>"']/g,function(m){
    return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#039;'}[m];
  });
}

loadTasks();
setMode();
updateStats();

setInterval(checkTaskReminders,10000);

setInterval(()=>{
  let highPending=tasks.filter(t=>t.priority==='high' && !t.done && t.state!=='skipped').length;
  let total=tasks.length;
  let done=tasks.filter(t=>t.done).length;
  let percent=total ? Math.round((done/total)*100) : 0;

  if(highPending>0 && percent<100 && smartMoodAllowed('periodic-high-reminder')){
    setMood('⚠️','High priority still waiting','reminder','Do not forget your high priority tasks');
    showRobotIcon('reminder');
  }
},600000);

setInterval(()=>{
  let hasRunning=tasks.some(t=>t.state==='in_progress');
  if(hasRunning){
    renderTasks();
  }
},1000);

setInterval(()=>{
  let changed=false;

  tasks.forEach(t=>{
    if(t.state==='in_progress'){
      stopTaskTimer(t);
      t.startedAt=Date.now();
      changed=true;
    }
  });

  if(changed){
    saveTasks();
  }
},30000);
</script>

</body>
</html>
)rawliteral";

  server.send(200, "text/html; charset=utf-8", page);
}

void handleTasks() {
  if (server.method() == HTTP_GET) {
    server.send(200, "application/json", tasksData);
    return;
  }

  if (server.method() == HTTP_POST) {
    if (server.hasArg("data")) {
      tasksData = server.arg("data");
      preferences.putString("tasks", tasksData);
      server.send(200, "text/plain", "Saved");
    } else {
      server.send(400, "text/plain", "Missing data");
    }
    return;
  }

  server.send(405, "text/plain", "Method not allowed");
}

void handleTimer() {
  String mode = server.arg("mode");

  if (mode == "start") {
    int minutes = server.arg("minutes").toInt();
    if (minutes < 1) minutes = 25;

    tabbieTimerEnd = millis() + ((unsigned long)minutes * 60000UL);
    focusMode = true;
    breakMode = false;

    currentMood = "focus";
    drawFace("focus");
    focusSound();

    server.send(200, "text/plain", "Timer started");
    return;
  }

  if (mode == "break") {
    tabbieTimerEnd = millis() + (5UL * 60000UL);
    focusMode = false;
    breakMode = true;

    currentMood = "break";
    drawFace("break");
    reminderSound();

    server.send(200, "text/plain", "Break started");
    return;
  }

  if (mode == "stop") {
    focusMode = false;
    breakMode = false;

    currentMood = "idle";
    drawFace("idle");

    server.send(200, "text/plain", "Timer stopped");
    return;
  }

  server.send(400, "text/plain", "Bad timer mode");
}

void handleMood() {
  if (server.hasArg("mood")) {
    currentMood = server.arg("mood");
    drawFace(currentMood);

    if (currentMood == "done") successSound();
    if (currentMood == "reminder") reminderSound();
    if (currentMood == "focus") focusSound();

    server.send(200, "text/plain", "Mood changed");
  } else {
    server.send(400, "text/plain", "Missing mood");
  }
}

void handleServo() {
  if (server.hasArg("angle")) {
    int angle = server.arg("angle").toInt();

    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;

    servoPos = angle;
    neck.write(servoPos);
  }

  server.send(200, "text/plain", "OK");
}

void handleStatus() {
  String json = "{";
  json += "\"status\":\"ok\",";
  json += "\"mood\":\"" + currentMood + "\",";
  json += "\"focusMode\":";
  json += (focusMode ? "true" : "false");
  json += ",";
  json += "\"breakMode\":";
  json += (breakMode ? "true" : "false");
  json += ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handleReset() {
  preferences.remove("ssid");
  preferences.remove("pass");

  server.send(200, "text/html; charset=utf-8", "<h1>WiFi reset. Restarting...</h1>");
  delay(1000);
  ESP.restart();
}


void handleRobotIcon() {
  String type = "idle";

  server.sendHeader("Access-Control-Allow-Origin", "*");

  if (server.hasArg("type")) {
    type = server.arg("type");
  } else if (server.hasArg("plain")) {
    String body = server.arg("plain");
    if (body.indexOf("done") >= 0) type = "done";
    else if (body.indexOf("start") >= 0) type = "start";
    else if (body.indexOf("focus") >= 0) type = "focus";
    else if (body.indexOf("note") >= 0) type = "note";
    else if (body.indexOf("add") >= 0) type = "add";
  }

  Serial.println("Mood: " + type);

  // ❗ بدل الرسم المباشر
  currentMood = type;
  moodUntil = millis() + 2000;

  server.send(200, "application/json", "{\"ok\":true}");
}
void updateRobot() {
  if (millis() - lastDraw < 100) return; // يمنع الفلاش
  lastDraw = millis();

  if (millis() < moodUntil) {
    drawRobotIcon(currentMood);
  } else {
    drawFace("idle");
  }
}
void drawRobotIcon(String type) {
  tft.fillScreen(GC9A01A_BLACK);

  uint16_t color = GC9A01A_CYAN;
  if (type == "add") color = GC9A01A_GREEN;
  if (type == "date" || type == "reminder") color = GC9A01A_YELLOW;
  if (type == "note") color = GC9A01A_WHITE;
  if (type == "start" || type == "focus") color = GC9A01A_CYAN;
  if (type == "done") color = GC9A01A_GREEN;
  if (type == "break") color = GC9A01A_BLUE;

  tft.drawCircle(120, 120, 116, color);
  tft.drawCircle(120, 120, 115, color);
  tft.setTextColor(color);
  tft.setTextWrap(false);

  if (type == "add") {
    tft.fillRoundRect(58, 58, 124, 124, 24, color);
    tft.fillRect(108, 78, 24, 84, GC9A01A_BLACK);
    tft.fillRect(78, 108, 84, 24, GC9A01A_BLACK);
    tft.setTextSize(2);
    tft.setCursor(78, 198);
    tft.print("ADDING");
  }
  else if (type == "date" || type == "reminder") {
    tft.drawRoundRect(56, 64, 128, 116, 12, color);
    tft.fillRect(56, 88, 128, 8, color);
    tft.fillRoundRect(78, 48, 16, 34, 8, color);
    tft.fillRoundRect(146, 48, 16, 34, 8, color);
    tft.setTextSize(5);
    tft.setCursor(93, 112);
    tft.print("!");
    tft.setTextSize(2);
    tft.setCursor(79, 198);
    if (type == "date") tft.print("TIME");
    else tft.print("ALERT");
  }
  else if (type == "note") {
    tft.drawRoundRect(62, 50, 116, 142, 10, color);
    tft.drawLine(82, 88, 158, 88, color);
    tft.drawLine(82, 116, 158, 116, color);
    tft.drawLine(82, 144, 142, 144, color);
    tft.setTextSize(2);
    tft.setCursor(86, 202);
    tft.print("NOTE");
  }
  else if (type == "start" || type == "focus") {
    tft.drawCircle(120, 116, 60, color);
    tft.fillTriangle(105, 82, 105, 150, 158, 116, color);
    tft.setTextSize(2);
    tft.setCursor(78, 198);
    if (type == "start") tft.print("START");
    else tft.print("FOCUS");
  }
  else if (type == "done") {
    tft.drawLine(62, 123, 101, 162, color);
    tft.drawLine(63, 124, 102, 163, color);
    tft.drawLine(101, 162, 180, 78, color);
    tft.drawLine(102, 163, 181, 79, color);
    tft.setTextSize(2);
    tft.setCursor(90, 198);
    tft.print("DONE");
  }
  else if (type == "break") {
    tft.fillRoundRect(70, 72, 86, 88, 14, color);
    tft.drawRoundRect(152, 92, 26, 42, 12, color);
    tft.fillRect(82, 160, 62, 8, color);
    tft.setTextSize(2);
    tft.setCursor(82, 198);
    tft.print("BREAK");
  }
  else {
    drawFace("idle");
  }
}

void drawIdleEyes(bool eyesOpen, int lookOffset) {
  tft.fillScreen(GC9A01A_BLACK);

  uint16_t color = GC9A01A_CYAN;

  int leftEyeX = 78 + lookOffset;
  int rightEyeX = 162 + lookOffset;
  int eyeY = 112;

  if (eyesOpen) {
    tft.fillRoundRect(leftEyeX - 30, eyeY - 24, 60, 48, 22, color);
    tft.fillRoundRect(rightEyeX - 30, eyeY - 24, 60, 48, 22, color);

    tft.fillCircle(leftEyeX + 6, eyeY + 2, 10, GC9A01A_BLACK);
    tft.fillCircle(rightEyeX + 6, eyeY + 2, 10, GC9A01A_BLACK);

    tft.fillCircle(leftEyeX + 1, eyeY - 5, 3, color);
    tft.fillCircle(rightEyeX + 1, eyeY - 5, 3, color);
  } else {
    tft.fillRoundRect(leftEyeX - 30, eyeY - 3, 60, 7, 4, color);
    tft.fillRoundRect(rightEyeX - 30, eyeY - 3, 60, 7, 4, color);
  }
}

void handleIdleAnimation() {
  static unsigned long lastMoveTime = 0;
  static unsigned long lastBlinkTime = 0;
  static bool blinking = false;
  static int lookState = 0;

  unsigned long now = millis();

  // حركة واحدة كل 1200ms
  if (now - lastMoveTime > 1200) {
    if (lookState == 0) {
      idleLookOffset = 12;   // يمين
      lookState = 1;
    } else {
      idleLookOffset = -12;  // يسار
      lookState = 0;
    }

    lastMoveTime = now;
  }

  // رمش كل 2600ms
  if (!blinking && now - lastBlinkTime > 2600) {
    blinking = true;
    lastBlinkTime = now;
  }

  if (blinking && now - lastBlinkTime > 120) {
    blinking = false;
    lastBlinkTime = now;
  }

  drawIdleEyes(!blinking, idleLookOffset);
}
void drawFace(String mood) {
  tft.fillScreen(GC9A01A_BLACK);

  uint16_t color = GC9A01A_CYAN;

  if (mood == "setup") color = GC9A01A_YELLOW;
  if (mood == "idle") color = GC9A01A_CYAN;
  if (mood == "focus") color = GC9A01A_WHITE;
  if (mood == "break") color = GC9A01A_BLUE;
  if (mood == "done") color = GC9A01A_GREEN;
  if (mood == "reminder") color = GC9A01A_YELLOW;

  tft.drawCircle(120, 120, 116, color);
  tft.drawCircle(120, 120, 115, color);

  if (mood == "setup") {
    tft.setTextColor(color);
    tft.setTextSize(3);
    tft.setCursor(43, 90);
    tft.print("SETUP");
    tft.setTextSize(2);
    tft.setCursor(34, 135);
    tft.print("192.168.4.1");
    return;
  }

  if (mood == "idle") {
    idleEyesOpen = true;
    idleLookOffset = 0;
    lastIdleAnimation = millis();
    drawIdleEyes(true, 0);
    return;
  }

  else if (mood == "focus") {
    tft.fillRoundRect(62, 88, 42, 18, 8, color);
    tft.fillRoundRect(136, 88, 42, 18, 8, color);
    tft.drawLine(85, 155, 155, 155, color);
    tft.drawLine(85, 156, 155, 156, color);
  }

  else if (mood == "break") {
    tft.fillRoundRect(58, 95, 48, 8, 4, color);
    tft.fillRoundRect(134, 95, 48, 8, 4, color);
    tft.drawCircle(120, 155, 18, color);
  }

  else if (mood == "done") {
    tft.fillCircle(80, 95, 14, color);
    tft.fillCircle(160, 95, 14, color);
    tft.drawCircle(120, 130, 50, color);
    tft.fillRect(60, 75, 120, 50, GC9A01A_BLACK);
    tft.setTextColor(color);
    tft.setTextSize(4);
    tft.setCursor(100, 155);
    tft.print("!");
  }

  else if (mood == "reminder") {
    tft.fillTriangle(120, 55, 55, 180, 185, 180, color);
    tft.fillTriangle(120, 85, 83, 160, 157, 160, GC9A01A_BLACK);
    tft.setTextColor(color);
    tft.setTextSize(4);
    tft.setCursor(108, 115);
    tft.print("!");
  }
}

void beep(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(120);
    digitalWrite(BUZZER_PIN, LOW);
    delay(120);
  }
}

void successSound() {
  beep(2);
}

void focusSound() {
  beep(1);
}

void reminderSound() {
  beep(3);
}
