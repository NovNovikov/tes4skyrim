ScriptName TES4_Attributes Hidden
{The player's eight TES4 attributes, one TES4Player<Attribute> global each.
MorrowindRuntime writes the attribute into its global and turns a script's
write into a change of the attribute. A global no plugin supplies reads as the
old stub, 100, so a gate on it falls open.}

Float Function Read(GlobalVariable attribute) Global
  If attribute
    Return attribute.GetValue()
  EndIf
  Return 100.0
EndFunction

Function Write(GlobalVariable attribute, Float value) Global
  If attribute
    attribute.SetValue(value)
  EndIf
EndFunction

Function Modify(GlobalVariable attribute, Float amount) Global
  If attribute
    attribute.SetValue(attribute.GetValue() + amount)
  EndIf
EndFunction
