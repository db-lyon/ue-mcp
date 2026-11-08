		MCPParam::Required(TEXT("factor"), EType::Number, TEXT("Time-scale factor, greater than 0 (e.g. 500)")),
	});
-----
		MCPParam::Required(TEXT("factor"), EType::Number, TEXT("Time-scale factor, greater than 0 (e.g. 500)")),
		MCPParam::Optional(TEXT("world"), EType::String, TEXT("pie (default) | game | auto")),
		MCPParam::Optional(TEXT("pieInstance"), EType::Number, TEXT("PIE world instance; omit for the first running one")),
	});
=====
		MCPParam::Optional(TEXT("playerIndex"), EType::Number, TEXT("0-based player index (default 0)")),
	});
-----
		MCPParam::Optional(TEXT("playerIndex"), EType::Number, TEXT("0-based player index (default 0)")),
		MCPParam::Optional(TEXT("world"), EType::String, TEXT("pie (default) | game | auto")),
		MCPParam::Optional(TEXT("pieInstance"), EType::Number, TEXT("PIE world instance; omit for the first running one")),
	});
=====
		MCPParam::Optional(TEXT("roll"), EType::Number, TEXT("Control-rotation roll")),
	}, MCPSpec::ContractExempt(TEXT("rotates the running PIE player's view")));
-----
		MCPParam::Optional(TEXT("roll"), EType::Number, TEXT("Control-rotation roll")),
		MCPParam::Optional(TEXT("world"), EType::String, TEXT("pie (default) | game | auto")),
		MCPParam::Optional(TEXT("pieInstance"), EType::Number, TEXT("PIE world instance; omit for the first running one")),
	}, MCPSpec::ContractExempt(TEXT("rotates the running PIE player's view")));
=====
		MCPParam::Optional(TEXT("showMouseCursor"), EType::Boolean, TEXT("Show the mouse cursor (default false for gameOnly, true otherwise)")),
	}, MCPSpec::ContractExempt(TEXT("sets the running PIE player's input mode")));
-----
		MCPParam::Optional(TEXT("showMouseCursor"), EType::Boolean, TEXT("Show the mouse cursor (default false for gameOnly, true otherwise)")),
		MCPParam::Optional(TEXT("world"), EType::String, TEXT("pie (default) | game | auto")),
		MCPParam::Optional(TEXT("pieInstance"), EType::Number, TEXT("PIE world instance; omit for the first running one")),
	}, MCPSpec::ContractExempt(TEXT("sets the running PIE player's input mode")));
