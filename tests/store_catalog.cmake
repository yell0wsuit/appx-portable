file(MAKE_DIRECTORY "${FIXTURE}/Assets")
file(COPY "${TEST}" "${SHIM}" DESTINATION "${FIXTURE}")
file(WRITE "${FIXTURE}/Assets/in-app-purchase.xml" [=[<?xml version="1.0" encoding="utf-8"?>
<CurrentApp><ListingInformation>
<Product ProductId="test.pack"/><Product ProductId="test.hints"/><Product ProductId="test.pack"/>
</ListingInformation><LicenseInformation><Product ProductId="not.a.listing"/></LicenseInformation></CurrentApp>
]=])
execute_process(COMMAND "${CMAKE_COMMAND}" -E env APPX_PORTABLE_UNLOCK_TRIAL=0 APPX_PORTABLE_STUB_IAP=0
    "${FIXTURE}/storetest.exe" --expect-stub-iap --expect-catalog --stub-iap
    WORKING_DIRECTORY "${FIXTURE}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Store catalog test failed: ${result}")
endif()
