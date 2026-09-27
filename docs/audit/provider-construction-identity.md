# Provider construction identity

Application constructors consume normalized skill, mode and client-count choices
in addition to provider metadata. Previously, changing those choices could reuse
an instance constructed with the previous values while publishing new choices.

The configuration manager now accepts an optional pure construction fingerprint
from its application owner. It combines that fingerprint with the existing
provider, artifact, resource, clock and explicit-option identity before deciding
whether to reuse private state. Failure follows the existing unpublished-owner
cleanup path. Identity version 3 separates this contract from older identities.

The application must derive its fingerprint and constructor arguments from the
same typed profile. Role routing remains snapshot-local and does not itself
invalidate private state. The application owner independently reviewed the
public hook and transaction changes and found no defect. Its concrete profile
consumer is still being written; this manager change alone does not complete
configuration/application acceptance. Verification was source review only.
