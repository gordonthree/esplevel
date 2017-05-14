var viewportWidth = $(window).width();

function addZero(i) {
    if (i < 10) {
        i = "0" + i;
    }
    return i;
}
$(document).on("pagebeforecreate",function(){
});

$(document).on("pagecreate",function() {
      // var hostName = "192.168.2.133"; // ip adress for ESP module
      var hostName = window.location.host; // get our address from the browser itself
      var nodeName;
      var sendUpdate=true;
      /*The user has WebSockets!!! */

      var smoothie = new SmoothieChart({
                          grid: {fillStyle:'rgba(13,39,62,0.78)', strokeStyle:'#4b8da3',
                          lineWidth: 1, millisPerLine: 250, verticalSections: 6, }
      });

      var line1 = new TimeSeries();
      var line2 = new TimeSeries();
      var line3 = new TimeSeries();

      smoothie.addTimeSeries(line1, { lineWidth:3, strokeStyle:'#00ff00' });
      smoothie.addTimeSeries(line2, { lineWidth:3, strokeStyle:'#ff8000' });
      smoothie.addTimeSeries(line3, { lineWidth:3, strokeStyle:'#0040ff' });
      smoothie.streamTo(document.getElementById("chart"), 100);

      connect(); // initate websockets client

      function connect(){
          var socket;
          var host = "ws://"+hostName+":81";

          try{
              var socket = new WebSocket(host);

              $("#footerText").html("Connecting...");
              message('<li class="event">Socket Status: '+socket.readyState);

              socket.onopen = function(){ // connect to websock server
             	 message('<li class="event">Socket Status: '+socket.readyState+' (open)');
               $("#footerText").html("Connected!");
              }

              socket.onmessage = function(msg){ // handle messages from the server
               var datamsg = msg.data + ''; // convert object to string
               var cmds = datamsg.split("="); // data is sent as label=data

               switch(cmds[0]) {
                case 'epoch':
                  var myD = new Date(cmds[1] * 1000);
                  var myDstr = myD.getUTCHours() + ":" + addZero(myD.getMinutes()) + ":" + addZero(myD.getSeconds());
                   $("#footerText").html("Device time: " + myDstr);
                  break;
                case 'name':
                  $("#headerText").html(cmds[1]);
                  nodeName=cmds[1];
                  document.title = cmds[1];
                  break;
                case 'temperature':
                  $("#headerText").html(nodeName+ "<BR>" +cmds[1]+ "&deg;C");
                  break;
                case 'bat':
                  $("#footer2").html("Battery " +cmds[1]+" volts");
                  break;
                case 'accelerometer':
                  var axis = cmds[1].split(",");
                  // update sliders
                  $( "#xaxis" ).val(axis[0]).slider("refresh"); // X axis
                  $( "#yaxis" ).val(axis[1]).slider("refresh"); // Y axis
                  $( "#zaxis" ).val(axis[2]).slider("refresh"); // Z axis
                  // update graph
                  line1.append(new Date().getTime(), axis[0]); // X
                  line2.append(new Date().getTime(), axis[1]); // Y
                  line3.append(new Date().getTime(), axis[2]); // Z
                  // $("#footer2").html("Accelerometer: " +cmds[1]);
                  break;
                default:
                  message('<li class="message">Msg: '+msg.data);
                }
              }


              socket.onclose = function(){ // websock server connection lost!
              	message('<li class="event">Socket Status: '+socket.readyState+' (Closed)');
                 $("#footerText","#headerText").html("Connection lost!");
                 $("#headerText").html("Disconnected");
                 setTimeout(connect(), 5000); // retry connection after 5 second delay
              }

          } catch(exception){
             message('<li>Error'+exception);
          }

          function message(msg){ // add entries to diagnostic log
            $('#diagList').prepend(msg+'</li>');
            $('#diagList').listview("refresh");
          }
      }//End connect

});
