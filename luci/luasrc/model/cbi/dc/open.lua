f = SimpleForm("x", "Door Controller", "Open the door!")

f1 = f:field(Value, "s", "Secret")

function f.handle(self, state, data)
        if state == FORM_VALID then
				local stat = (data.s == "test")
                
                if stat then
                        f.message = translate("Door opened")
                else
                        f.errmessage = translate("Unknown Error")
                end
        end
        return true
end

return f
