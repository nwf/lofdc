module("luci.controller.dc.dc", package.seeall)

function index()
	-- Register self in header
	entry({"dc"}, alias("dc", "open"), "Door", 30)

	-- cbi interface to door
	entry({"dc", "open"}, cbi("dc/open"), "Open", 30).dependent=false

	-- API
	page = entry({"dc", "oapi"}, call("openapi"), "Open", 40)
	page.dependent=false
	page.leaf=true

	-- webcam
	entry({"dc", "webcam"}, template("dc/webcam"), "Webcam", 25).dependent=false
end

function openapi()
    local path = luci.dispatcher.context.requestpath
    local arg  = path[#path]

	luci.http.redirect(luci.dispatcher.build_url("dc/open?=" .. dev))
end
