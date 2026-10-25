if(NOT DEFINED CATALOG)
    set(CATALOG "Assets/in-app-purchase.xml")
endif()
get_filename_component(catalog_directory "${FIXTURE}/${CATALOG}" DIRECTORY)
file(MAKE_DIRECTORY "${catalog_directory}")
file(COPY "${TEST}" "${SHIM}" DESTINATION "${FIXTURE}")
file(WRITE "${FIXTURE}/AppxManifest.xml" [=[<Package>
<Identity Name="StoreCatalogFixture" Version="1.0.0.0" ProcessorArchitecture="x86"/>
</Package>]=])
file(WRITE "${FIXTURE}/unrelated.xml" [=[<Assets><ListingInformation>
<Product ProductId="not.a.catalog"/></ListingInformation></Assets>]=])
file(WRITE "${FIXTURE}/malformed.xml" [=[<CurrentApp><ListingInformation>
<Product ProductId="not.valid.xml"/></ListingInformation>]=])
file(WRITE "${FIXTURE}/${CATALOG}" [=[<?xml version="1.0" encoding="utf-8"?>
<CurrentApp><ListingInformation>
<Product ProductId="test.pack"/><Product ProductId="test.hints"/><Product ProductId="test.pack"/>
<Unrelated><Product ProductId="not.a.direct.product"/></Unrelated>
</ListingInformation><LicenseInformation><Product ProductId="not.a.listing"/></LicenseInformation></CurrentApp>
]=])
execute_process(COMMAND "${CMAKE_COMMAND}" -E env APPX_PORTABLE_UNLOCK_TRIAL=0 APPX_PORTABLE_STUB_IAP=0
    "${FIXTURE}/storetest.exe" --expect-stub-iap --expect-catalog --stub-iap
    WORKING_DIRECTORY "${FIXTURE}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Store catalog test failed: ${result}")
endif()
