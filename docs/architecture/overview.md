# Architecture Overview

This document provides a high-level overview of the `photo_indexer` architecture.

## Component Diagram

The following diagram shows the main components of the `photo_indexer` system:

```mermaid
flowchart TD
    subgraph PI [Photo Indexer]
        FS[File Scanner] --> ME[Metadata Extractor]
        ME --> FB[FlatBuffers Serializer]
        FB --> IG[Index Generator]
    end

    subgraph PC [Photo Collection]
        PF[Photo Files]
    end

    subgraph MS [Metadata Storage]
        MB[(metadata.bin)]
        IISO[(index_iso.bin)]
        IDATE[(index_date.bin)]
        ICAM[(index_camera.bin)]
        ITAG[(index_tags.bin)]
    end

    PF --> FS
    IG --> MB
    IG --> IISO
    IG --> IDATE
    IG --> ICAM
    IG --> ITAG
```

## Sequence Diagram

The processing sequence for each photo is illustrated below:

```mermaid
sequenceDiagram
    participant FS as File Scanner
    participant ME as Metadata Extractor (Exiv2)
    participant FB as FlatBuffers Serializer
    participant IG as Index Generator
    participant DB as Binary Storage

    FS->>ME: Path to Photo
    ME->>ME: Extract EXIF, IPTC, XMP
    ME-->>FB: Raw Metadata Structure
    FB->>FB: Serialize to FlatBuffers
    FB-->>IG: FlatBuffers Buffer
    IG->>IG: Update ISO, Date, Camera, Tag Indexes
    IG->>DB: Write Metadata & Index Files
```

## Data Model

The data model is defined in the FlatBuffers schema located at `schemas/metadata.fbs`. It is designed to be:
- **Efficient**: Minimal memory overhead and fast access.
- **Extendable**: New metadata fields can be added without breaking compatibility.
- **Standalone**: All necessary information for the gallery backend is contained in the binary files.
