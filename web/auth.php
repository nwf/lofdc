<!-- Based on http://www.emblematiq.com/lab/niceforms/demo/niceforms.html -->
<script language="javascript" type="text/javascript" src="niceforms.js"></script>
<link rel="stylesheet" type="text/css" media="all" href="niceforms-default.css" />
<body>
<form action="index.php" method="post" class="niceform">
    <fieldset>
    	<legend>Login to unlock Door</legend>
        <dl>
        <dt><label for="email">Email:</label></dt>
            <dd><input type="text" name="email" id="email" size="32" maxlength="128" /></dd>
        </dl>
        <dl>
        	<dt><label for="password">Password:</label></dt>
            <dd><input type="password" name="pw" id="password" size="32" maxlength="32" /></dd>
        </dl>
        <dl>
    </fieldset>
    <fieldset class="action">
    	<center><input type="submit" name="submit" id="submit" value="Unlock!" /></center>
    </fieldset>
</form>
<?php
//print "test";

?>
</body>
