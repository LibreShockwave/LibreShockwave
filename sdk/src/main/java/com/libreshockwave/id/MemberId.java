package com.libreshockwave.id;

/**
 * Director member slot number within a cast library.
 * Member 0 is a valid "empty member" reference even though real stored
 * cast members begin at slot 1.
 */
public record MemberId(int value) implements TypedId, Comparable<MemberId> {

    public MemberId {
        if (value < 0) {
            throw new IllegalArgumentException("MemberId must be >= 0, got " + value);
        }
    }

    @Override
    public int compareTo(MemberId other) {
        return Integer.compare(this.value, other.value);
    }

    @Override
    public String toString() {
        return "MemberId(" + value + ")";
    }
}
