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
	</head>

<!-- ********* UNIVERSAL CONTENT ********* -->
	<body onLoad="reloadImg('webcam');">
		<div id="centerDiv"> 	
			<div id="logo">
				<a href="some location"><img src="media/logo.png" border="0" alt="//media"></a>					
			</div>

			<div id="webcambox">
				<img src="http://baltimorenode.redirectme.net:3456/webcam.jpg?v=123" id='webcam' border="0" alt="//media">
			</div>

			<div id="loginbox">
				<a href="login.php">
					<div id="logintext"><h1> Login </h1></div>	
				</a>
			</div>

			<div id="unlockbox">
				<a href="#" onClick="alert('unlock?');">
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
						$db=new PDO('sqlite:./media/door.db');
						$sql = "SELECT * FROM users";
						foreach ($db->query($sql)as $row)
					{
						echo "User ";
						print $row['user_id'] . ' - ' . $row['admin']  . ' - ' . $row['email'] . '<br/>';
					}
						echo "John Smith - 0 - icouldbeanyone@email.com<br/>";
						echo "Peter Johnson - 0 - jeebuslong@email.com <br/>";
						echo "Mary JoHanson - 0 - mynameismary@email.com <br/>";
					?>
				</div>

				<div id="timestamp">
					<?php
						$db=new PDO('sqlite:./media/door.db');
						$sql = "SELECT * FROM log";
						foreach ($db->query($sql)as $row)
					{
						print $row['message'] . '<br/>';
					}
						echo "4pm <br/>";
			
					?>
				</div>

				<div id="timestampheader">
					<h3> Time In</h3>
				</div>
			</div>
			
			<div id="activitytext">
				<h2> Recent Activity </h2>
			</div>
		</div><!-- end centerDiv -->
	</body>
</html>
