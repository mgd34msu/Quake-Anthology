# Authoritative collision metadata across providers

The Q3 guest record adapter exposed a missing shared-world contract: contents,
model and owner fields are authoritative between links. A typed borrowed
collision reader now supplies that metadata for the selected actor. Broadphase
bounds and insertion order remain retained until link. No all-entity polling or
second collision store is introduced. Stored policies remain available as a
fallback after explicit unbind; updating fallback data does not detach a guest
reader. Body release clears the binding before provider destruction.

The getter checks actor/body/binding identity around callbacks and validates
returned policy. Link publication also rejects nested changes to the link it
was about to replace. Reader failure is distinguishable from absent metadata
through world traces, point queries, visits, trigger dispatch and consumers.
Movement BSP classification and ground admission now separate failure from
an ordinary negative gameplay answer. Q1 grapple callers reacquire hook and
player generations after the new metadata callback.

Independent review of the root change found two surrounding interoperability
issues. Raw Q3 entity/owner numbers now compare only inside the same canonical
actor-owner domain; foreign providers use canonical actor IDs. Q3 server
point-contents requests explicitly select temporary-brush behavior: Q3 boxes
contribute BODY; Q3 capsule handles rotate the point and test the source brush
bounds. Ordinary shared point queries keep their existing contents semantics;
foreign families retain contents conversion. Root checked guest-records.ts,
guest-spatial.ts, the shared collision index and temporary model.ts to distinguish
these paths.

The Q3 host owner independently reread the namespace correction, explicit query
mode, temporary-model transform and nested-link guard and found no further
defect in that bounded diff.

The Q3 host consumes these APIs while it is still under construction. Complete
guest application and save consumers remain open. This correction does not
establish acceptance of every original B06/B08/B23/B34 criterion. Validation here
is source review and whitespace checking only; engine execution remains deferred.
