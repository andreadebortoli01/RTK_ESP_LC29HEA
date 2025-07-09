#include <WebServer.h>

extern WebServer server;

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += R"rawliteral(
  <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>
  <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Montserrat:wght@600;900&family=Fira+Mono&display=swap');
    body { margin: 0; padding: 0; min-height: 100vh;
      background: linear-gradient(120deg,#161e2a 0%, #13151a 100%);
      color: #f6fcff; font-family: 'Montserrat', Arial, sans-serif;
      display: flex; flex-direction: column; align-items: center;}
    .container { width: 100%; max-width: 780px; margin: 0 auto;
      display: flex; flex-direction: column; align-items: center; }
    h1 { font-size: 2.3em; font-weight: 900; color: #6ee8f9;
      margin: 28px 0 10px 0; letter-spacing: 1.5px;
      text-shadow: 0 3px 18px #239ddb40;}
    .datagrid { width: 100%; max-width: 700px; background: #181b22ee;
      border-radius: 20px; box-shadow: 0 0 30px #132b4488;
      margin-bottom: 30px; overflow: hidden;}
    table { border-collapse: collapse; width: 100%; font-size: 1.18em;}
    th, td { padding: 16px 12px; text-align: center;}
    th { background: #222634; color: #56ffe8; font-size: 1em;
      letter-spacing: 0.5px; font-weight: 700;
      text-shadow: 0 2px 10px #22fdff13;
      border-bottom: 2px solid #5ad5ff22;}
    td { background: #191c25bb; color: #fff; font-size: 1em; font-weight: 700;
      border-bottom: 1px solid #2c2e3a40;}
    tr:last-child td, tr:last-child th { border-bottom: none;}
    .value { font-weight: 800; font-size: 1.09em;}
    .fix { color: #ffe877; text-shadow: 0 1px 10px #ffc20055;}
    .rtk { color: #57ffd7;}
    .badge { display: inline-block; padding: 2px 10px; border-radius: 16px;
      font-size: 0.98em; margin-left: 6px; background: #191f26; color: #a3faff; box-shadow: 0 1px 8px #29e0e533;}
    #map { width: 99vw; max-width: 760px; height: 320px; border-radius: 22px; margin: 0 auto 18px auto;
      border: 3px solid #20b9c6; box-shadow: 0 2px 30px #0ae6ff40; background: #16222e;}
    #epe3dChart { background: #191c24; border-radius: 17px; box-shadow: 0 0 10px #29f3fc55; margin-top: 7px;}
    .button-row { margin: 22px 0 16px 0; display: flex; justify-content: center; gap: 16px;}
    .btnlog { font-family: 'Montserrat', sans-serif; padding: 12px 28px; font-size: 1.13em;
      border-radius: 11px; border: none; font-weight: 800; cursor: pointer;
      box-shadow: 0 3px 16px #24aaf322; background: #2a3043; color: #aaffee;
      transition: .16s; outline: none; border: 2px solid #2aebb3; position: relative; z-index: 2;}
    .btnlog.active { background: #29f1bc; color: #181a20; border-color: #4ef3c6; box-shadow: 0 2px 17px #24e3e970;}
    .btncsv { background: linear-gradient(90deg,#52dfff 60%,#0f8bf9 100%); color: #1b1b23; border: 0; font-weight: 900; text-shadow: 0 2px 8px #1cc2ff55;}
    .btnlog:active { transform: scale(0.98);}
    @media (max-width:700px){ .datagrid { font-size: 0.99em; } #map { height: 160px;} h1 { font-size: 1.2em; } }
  </style>
  )rawliteral";
  html += "<body><div class='container'><h1>RTK GNSS LIVE</h1>";
  html += "<div class='datagrid'><table id='gnsstable'>";
  html += "<tr><th>Latitudine</th><td class='value' id='lat'>...</td></tr>";
  html += "<tr><th>Longitudine</th><td class='value' id='lon'>...</td></tr>";
  html += "<tr><th>Altitudine</th><td class='value' id='alt'>...</td></tr>";
  html += "<tr><th>Satelliti</th><td class='value' id='sats'>...</td></tr>";
  html += "<tr><th>Fix</th><td class='value fix' id='fix'>...</td></tr>";
  html += "<tr><th>Fase RTK</th><td class='value rtk' id='rtk'>...</td></tr>";
  html += "<tr><th>Precisione 3D</th><td class='value' id='epe3d'>...</td></tr>";
  html += "<tr><th>Età correzione</th><td class='value' id='agediff'>...</td></tr>";
  html += "<tr><th>Bytes RTCM</th><td class='value' id='rtcm'>...</td></tr>";
  html += "<tr><th>Stato GNSS</th><td id='status'>...</td></tr>";
  html += "<tr><th>Stato NTRIP</th><td id='ntrip'>...</td></tr>";
  html += "<tr><th>Stato WiFi</th><td class='value' id='wifi'>...</td></tr>";
  html += "<tr><th>Velocità</th><td class='value' id='velocita'>...</td></tr>";
  // ---- AGGIUNTA RIGA CPU USAGE ----
  html += "<tr><th>Utilizzo CPU</th><td class='value' id='cpuusage'>...</td></tr>";
  // ---------------------------------
  html += "</table></div>";

  html += "<div class='button-row'><button class='btnlog' id='btnStart'>⏺ Avvia Log</button>";
  html += "<button class='btnlog btncsv' id='btnDownload' style='display:none'>⬇ Scarica CSV</button>";
  html += "<button class='btnlog' id='btnClear' style='display:none'>🧹 Reset Log</button></div>";

  html += "<div id='map'></div>";
  html += "<h3>Grafico Precisione 3D</h3><canvas id='epe3dChart' width='720' height='155'></canvas>";

  html += R"rawliteral(
<script>
let map, marker, poly;
let logData = JSON.parse(localStorage.getItem('gnssLog') || '[]');
let logActive = localStorage.getItem('gnssLogActive')==="1";
let logStartTime = parseInt(localStorage.getItem('gnssLogStartTime')||"0");
let btnStart, btnDownload, btnClear, ctx, epe3dChart;
let epe3dHist = JSON.parse(localStorage.getItem('epe3dHist')||'[]');
let lastCoords = null;

function updateButtons() {
  btnStart.textContent = logActive ? "⏸ Ferma Log" : "⏺ Avvia Log";
  btnStart.classList.toggle("active", logActive);
  btnDownload.style.display = logData.length>0 ? "" : "none";
  btnClear.style.display = logData.length>0 ? "" : "none";
}

function updateMap(lat,lon){
  if(!map){
    map = L.map('map').setView([lat,lon], 20);
    L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{maxZoom:22, attribution: ''}).addTo(map);
    map.on('moveend', function() { });
  }
  lastCoords = [lat,lon];
  if(marker){ marker.remove(); }
  marker = L.circleMarker([lat, lon],{radius:13, fillColor:'#13ffe2',color:'#0cc',weight:3,fillOpacity:0.9}).addTo(map);
}

function updateChart(){
  if(!ctx){ ctx = document.getElementById('epe3dChart').getContext('2d'); }
  if(epe3dChart){ epe3dChart.destroy(); }
  let labels = [];
  for(let i=0;i<epe3dHist.length;i++) labels.push('');
  epe3dChart = new Chart(ctx, {
    type: 'line',
    data: {
      labels: labels,
      datasets: [{
        label: 'Precisione 3D (m)',
        data: epe3dHist,
        borderColor: 'rgba(13,255,240,1)',
        backgroundColor: 'rgba(13,255,240,0.12)',
        fill: true,
        tension: 0.21,
        pointRadius: 0,
        borderWidth: 3,
      }]
    },
    options: {
      plugins: { legend: {display: false} },
      animation: false,
      scales: {
        x: {display:false},
        y: {
          beginAtZero:true,
          ticks: {color:'#aefcfe'},
          grid:{color:'#24494c66'}
        }
      }
    }
  });
}

function updateTable(d){
  document.getElementById('lat').textContent = (d.lat!==undefined ? d.lat.toFixed(8)+" °" : "...");
  document.getElementById('lon').textContent = (d.lon!==undefined ? d.lon.toFixed(8)+" °" : "...");
  document.getElementById('alt').textContent = (d.alt!==undefined ? d.alt.toFixed(2)+" m" : "...");
  document.getElementById('sats').textContent = d.sats!==undefined ? d.sats : "...";
  document.getElementById('fix').textContent = d.fix ? d.fix : "...";
  document.getElementById('rtk').textContent = d.rtk||"...";
  document.getElementById('epe3d').textContent = (d.epe3d!==undefined && d.epe3d>=0 ? d.epe3d.toFixed(3)+" m" : "N/D");
  document.getElementById('agediff').textContent = (d.agediff!==undefined && d.agediff>=0 ? d.agediff.toFixed(1)+" s" : "N/D");
  document.getElementById('rtcm').textContent = d.rtcm!==undefined ? d.rtcm + " B" : "...";
  document.getElementById('status').textContent = d.status||"...";
  document.getElementById('ntrip').textContent = d.ntrip||"...";
  document.getElementById('wifi').textContent = d.rssi!==undefined ? (d.rssi + " dBm") : "...";
  document.getElementById('velocita').textContent = (d.vel!==undefined ? d.vel.toFixed(2) + " m/s" : "...");
  // ---- AGGIUNTA JS: mostra CPU usage ----
  document.getElementById('cpuusage').textContent = (d.cpuusage!==undefined ? d.cpuusage.toFixed(1)+" %" : "...");
  // ---------------------------------------
}

function fetchData(){
  fetch('/api').then(r=>r.json()).then(d=>{
    if(!d || d.lat===undefined || Math.abs(d.lat)<0.01 || Math.abs(d.lon)<0.01){
      document.getElementById('map').style.display='none';
      return;
    } else {
      document.getElementById('map').style.display='';
    }
    updateTable(d);
    if(logActive){
      let now = Math.round(Date.now()/1000);
      if(!logStartTime){logStartTime=now;localStorage.setItem('gnssLogStartTime',logStartTime);}
      // log: tempo, lat, lon, alt, epe3d
      logData.push([now-logStartTime, d.lat, d.lon, d.alt, d.epe3d]);
      localStorage.setItem('gnssLog', JSON.stringify(logData));
    }
    if(d.epe3d!==undefined && !isNaN(d.epe3d)){
      epe3dHist.push(d.epe3d);
      if(epe3dHist.length>100) epe3dHist = epe3dHist.slice(epe3dHist.length-100);
      localStorage.setItem('epe3dHist',JSON.stringify(epe3dHist));
    }
    updateMap(d.lat, d.lon);
    updateChart();
  });
}
window.onload = function(){
  btnStart = document.getElementById('btnStart');
  btnDownload = document.getElementById('btnDownload');
  btnClear = document.getElementById('btnClear');
  updateButtons();
  fetchData();
  setInterval(fetchData, 100);
  btnStart.onclick = function(){
    logActive = !logActive;
    localStorage.setItem('gnssLogActive', logActive ? "1":"0");
    if(!logActive) localStorage.setItem('gnssLogStartTime', "");
    updateButtons();
  };
  btnDownload.onclick = function(){
    if(logData.length==0) return;
    let csv = "t_s,lat,lon,alt,epe3d\n";
    logData.forEach(e=>{
      csv += e[0]+","+e[1]+","+e[2]+","+e[3]+","+e[4]+"\n";
    });
    let blob = new Blob([csv], {type: "text/csv"});
    let url = URL.createObjectURL(blob);
    let a = document.createElement('a');
    a.href = url; a.download = "gnss_log.csv"; document.body.appendChild(a);
    a.click(); setTimeout(()=>{URL.revokeObjectURL(url);a.remove();},300);
  };
  btnClear.onclick = function(){
    localStorage.removeItem('gnssLog');
    localStorage.removeItem('gnssLogStartTime');
    logData = [];
    updateButtons();
    location.reload();
  };
};
</script>
</body></html>
)rawliteral";
  server.send(200, "text/html", html);
}