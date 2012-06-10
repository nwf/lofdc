<?php
require_once("globalVars.php");
if(isset($_POST['email']) && isset($_POST['pw'])){
	
	print $_POST["email"];
	print "<br>";
	print $_POST["pw"];
	print "starting socket<br>";
	$fp = fsockopen("lofdoorcontrol.local", 12321, $errno, $errstr, 30);
	print "opened socket<br>";
	$b=0;
	if (!$fp) {
	    echo "$errstr ($errno)<br />\n";
	} else {
	    $out = "auth admin@example.com admin\r\n";
	    //$out = "ping\n";
	    fwrite($fp, $out);
	    print "sent<br>";
	    print fgets($fp, 15);
	    /*while (!feof($fp)) {
		print fgets($fp, 4);
	    }*/
	    fclose($fp);
	}
}


?>
<html>
<!-- ********* STYLESHEET LINK ********* -->
	<head>
		<LINK REL=StyleSheet HREF="styles.css" TYPE="text/css" MEDIA=screen>
		<title> The Node: Door </title> 
		<script>
		// Thanks: http://stackoverflow.com/questions/1077041/refresh-image-with-a-new-one-at-the-same-url
		function reloadImg() { 
			id="webcam";
			var obj = document.getElementById(id);
			var src = obj.src;
			var pos = src.indexOf('?');
			if (pos >= 0) {
			src = src.substr(0, pos);
			}
			var date = new Date();
			obj.src = src + '?v=' + date.getTime();
			
			setTimeout(reloadImg, 1000);
		}
		</script>
		<script language="javascript" src="js/jquery.js"></script>
		<script language="javascript" src="js/modal.popup.js"></script>
		<script language="javascript">
			//http://www.jquerypopup.com/demo.php
		     $(document).ready(function() {
			    
				//Change these values to style your modal popup
				var align = 'center';										//Valid values; left, right, center
				var top = 100; 												//Use an integer (in pixels)
				var padding = 10;											//Use an integer (in pixels)
				var backgroundColor = '#FFFFFF'; 							//Use any hex code
				var borderColor = '#000000'; 								//Use any hex code
				var borderWeight = 4; 										//Use an integer (in pixels)
				var borderRadius = 5; 										//Use an integer (in pixels)
				var fadeOutTime = 300; 										//Use any integer, 0 = no fade
				var disableColor = '#666666'; 								//Use any hex code
				var disableOpacity = 40; 									//Valid range 0-100
				var loadingImage = 'lib/release-0.0.1/loading.gif';	//Use relative path from this page
					
				//This method initialises the modal popup
			$(".modal").click(function() {
					
					var source = 'auth.php';	//Refer to any page on your server, external pages are not valid
					var width = 350; 					//Use an integer (in pixels)
			    
					modalPopup(align, top, width, padding, disableColor, disableOpacity, backgroundColor, borderColor, borderWeight, borderRadius, fadeOutTime, source, loadingImage);
					
			});
				
				//This method hides the popup when the escape key is pressed
				$(document).keyup(function(e) {
					if (e.keyCode == 27) {
						closePopup(fadeOutTime);
					}
				});
				
		    });
			
		</script>
	</head>

<!-- ********* UNIVERSAL CONTENT ********* -->
	<body onLoad="reloadImg('webcam');">
		<div id="centerDiv"> 	
			<div id="logo">
				<a href="some location"><img src="media/logo.png" border="0" alt="//media"></a>					
			</div>

			<div id="webcambox">
				<img src="<?php print $webcamLoc;?>?v=123" id='webcam' border="0" alt="//media">
			</div>

			<div id="loginbox">
				<a href="login.php">
					<div id="logintext"><h1> Login </h1></div>	
				</a>
			</div>

			<div id="unlockbox">
				<a class="modal" href="javascript:void(0);">
					<div id="unlocktext"><h1> Unlock </h1></div>
				</a>
			</div>				
				
	
<!-- ********* ACTIVITY CONTENT ********* -->
			<div id="activity">
				<div id="usersheader">
					<h3> Recent Users - Admin - E-Mail</h3>
				</div>

				<div id="users">
					<?php
						try {
							// TODO: implement using $usr and $pw
							$db = new PDO('sqlite:'.$dbFile);
							//$sql = "SELECT * FROM users";
							$sql = "SELECT users.user_id, users.email, log.message from users, log where users.user_id=log.user_id";
						} catch (PDOException $e) {
							print "Error!: " . $e->getMessage() . "<br/>";
							die();
						}
						
						foreach ($db->query($sql)as $row)
						{
							if($row['user_id']==0)
							{
								$users_name="Joe Blow";
							}
							elseif($row['user_id']==1)
							{
								$users_name="Sue Blue";
							}
							print $users_name . ' - ' . $row['email'] .  '<br/>';
						}
					?>
				</div>

				<div id="timestamp">
					<?php
						try {
							// TODO: implement using $usr and $pw
							$db=new PDO('sqlite:'.$dbFile);
							$sql = "SELECT message FROM log";
						} catch (PDOException $e) {
							print "Error!: " . $e->getMessage() . "<br/>";
							die();
						}
						
						foreach ($db->query($sql)as $row)
						{
							print $row['message'] . '<br/>';
						}
			
					?>
				</div>
				
				<div id="timestampheader">
					<h3> Time In</h3>
				</div>
			</div>
			
		</div><!-- end centerDiv -->
	</body>
</html>
